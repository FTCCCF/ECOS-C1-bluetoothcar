param(
    [ValidateSet('model','car','diagnostic')][string]$Mode='model',
    [ValidatePattern('^[A-Za-z]$')][string]$DriveLetter,
    [string]$FirmwarePath
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'hfp-state.ps1')
$repo=Split-Path -Parent $PSScriptRoot
$fileName='c1_voice_'+$Mode+'.bin'
$firmware=Join-Path $repo ('build\voice\'+$fileName)
if($FirmwarePath){
    $firmware=(Resolve-Path -LiteralPath $FirmwarePath -ErrorAction Stop).Path
    if((Split-Path -Leaf $firmware) -ne $fileName){throw 'Explicit firmware must have the production filename matching -Mode; simulation images are not downloadable.'}
}
if(!(Test-Path -LiteralPath $firmware)){throw "Firmware missing: $firmware"}
if(!$DriveLetter){
    $volumes=@(Get-Volume | Where-Object {$_.DriveLetter -and $_.FileSystemLabel -match '(?i)YSYX|HFP'})
    if($volumes.Count -ne 1){throw 'Connect C1 in MCU mode; specify -DriveLetter only for its HFP-LINK download volume.'}
    $DriveLetter=[string]$volumes[0].DriveLetter
}
$targetRoot=$DriveLetter+':\'
$volume=Get-Volume -DriveLetter $DriveLetter
if($volume.FileSystemLabel -notmatch '(?i)YSYX|HFP'){throw 'Target is not identified as an HFP-LINK download volume.'}
$stateFile=Join-Path $targetRoot 'STATE.TXT'
if(!(Test-Path -LiteralPath $stateFile)){throw 'HFP-LINK STATE.TXT was not found.'}
$successPattern='(?im)^\s*write\s+successful\s*!*\s*$|^\s*success\s*!*\s*$'
$stateBefore=Read-HfpState $stateFile
if($stateBefore -match $successPattern){throw 'HFP-LINK already reports a completed download. Disconnect and reconnect its USB cable in MCU mode before another write.'}
$destination=Join-Path $targetRoot $fileName
$sourceHash=(Get-FileHash -LiteralPath $firmware -Algorithm SHA256).Hash
Copy-Item -LiteralPath $firmware -Destination $destination
$destinationHash=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
if($sourceHash -ne $destinationHash){throw 'Downloaded file hash differs from the source.'}
# Commit only this programmer's pending filesystem writes before status polling.
Write-VolumeCache -DriveLetter $DriveLetter -ErrorAction Stop
$state=''
for($attempt=0;$attempt -lt 20;$attempt++){
    Start-Sleep -Milliseconds 500
    $state=Read-HfpState $stateFile
    if($state -match $successPattern) {break}
}
if($state -notmatch $successPattern){throw "Downloader status: $state"}
$output=Join-Path $repo 'tests\results'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$report=[ordered]@{
    timestamp=[DateTimeOffset]::Now.ToString('o');mode=$Mode;firmware=$firmware;
    downloaded_file_sha256=$destinationHash;file_copy_verified=$true;
    downloader_state_before=$stateBefore.Trim();downloader_state=$state.Trim();
    downloader_state_read='FILE_FLAG_NO_BUFFERING';board_flashed=$true;
    board_inference_verified=$false;car_motion_verified=$false
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'flash-result.json') -Encoding utf8
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output ('flash-'+$Mode+'-result.json')) -Encoding utf8
Write-Host "Copied $fileName; SHA256=$sourceHash"
Write-Host $state.Trim()
if($Mode -eq 'diagnostic'){
    Write-Host 'Power off, switch FLASH_SEL to CHIP, reconnect USB and power on; use scripts/board_diag.py.'
}else{
    Write-Host 'Power off, switch FLASH_SEL to CHIP, reconnect USB and power on; use scripts/read-board.ps1.'
}
