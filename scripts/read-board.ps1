param([string]$PortName='COM5',[ValidateRange(5,600)][int]$Seconds=30)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$output=Join-Path $repo 'tests\results'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$log=Join-Path $output ('board-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss')+'.log')
$port=[IO.Ports.SerialPort]::new($PortName,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadTimeout=1000;$port.WriteTimeout=2000
$lines=[Collections.Generic.List[string]]::new()
try {
    $port.Open();$port.Write('i')
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt $Seconds){
        try {
            $line=$port.ReadLine().TrimEnd("`r")
            Write-Host $line;$lines.Add($line)
        } catch {
            $rootException=$_.Exception
            while($rootException.InnerException){$rootException=$rootException.InnerException}
            if($rootException -isnot [TimeoutException]){throw}
        }
    }
} finally {
    if($port.IsOpen){$port.Close()};$port.Dispose()
    [IO.File]::WriteAllLines($log,$lines,[Text.UTF8Encoding]::new($false))
}
Write-Host "Board UART log saved: $log"
if(!($lines | Where-Object {$_ -match '^RESULT class='})){throw 'No board inference result received; inspect the firmware identity and power/mode first.'}
