# HFP-LINK changes STATE.TXT on the device without notifying Windows' file cache.
# Read an aligned block with FILE_FLAG_NO_BUFFERING so an earlier read cannot
# hide the current downloader status. This helper only reads the named file.
if (-not ('C1HfpStateReader' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
public static class C1HfpStateReader {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool ReadFile(SafeFileHandle file, IntPtr buffer, uint size, out uint read, IntPtr overlapped);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr VirtualAlloc(IntPtr address, UIntPtr size, uint allocation, uint protect);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool VirtualFree(IntPtr address, UIntPtr size, uint type);
    public static string Read(string path) {
        using (var file=CreateFile(path,0x80000000,3,IntPtr.Zero,3,0x20000000,IntPtr.Zero)) {
            if(file.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr buffer=VirtualAlloc(IntPtr.Zero,(UIntPtr)4096,0x3000,4);
            if(buffer==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            try {
                uint count;
                if(!ReadFile(file,buffer,4096,out count,IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
                byte[] bytes=new byte[count]; Marshal.Copy(buffer,bytes,0,(int)count);
                return Encoding.ASCII.GetString(bytes).TrimEnd('\0','\r','\n');
            } finally { VirtualFree(buffer,UIntPtr.Zero,0x8000); }
        }
    }
}
'@
}
function Read-HfpState([string]$Path) {
    [C1HfpStateReader]::Read($Path)
}
