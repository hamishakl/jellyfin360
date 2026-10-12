# Installs MinGit (portable Git for Windows) to C:\Tools\git and clones the repo to C:\src.
$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
if (-not (Test-Path C:\Tools\git\cmd\git.exe)) {
    $rel = Invoke-RestMethod https://api.github.com/repos/git-for-windows/git/releases/latest -TimeoutSec 30
    $asset = $rel.assets | Where-Object { $_.name -like 'MinGit-*-64-bit.zip' -and $_.name -notlike '*busybox*' } | Select-Object -First 1
    'Downloading ' + $asset.name
    Invoke-WebRequest -UseBasicParsing $asset.browser_download_url -OutFile $env:TEMP\mingit.zip -TimeoutSec 600
    New-Item -ItemType Directory -Force C:\Tools\git | Out-Null
    Expand-Archive $env:TEMP\mingit.zip C:\Tools\git -Force
}
$path = [Environment]::GetEnvironmentVariable('Path', 'Machine')
if ($path -notlike '*C:\Tools\git\cmd*') {
    [Environment]::SetEnvironmentVariable('Path', $path + ';C:\Tools\git\cmd', 'Machine')
}
$env:Path += ';C:\Tools\git\cmd'
git --version
git config --global user.name 'Your Name'
git config --global user.email 'you@example.com'
git config --global core.autocrlf false
New-Item -ItemType Directory -Force C:\src | Out-Null
if (-not (Test-Path C:\src\jellyfin360\.git)) {
    git clone -q https://github.com/hamishakl/jellyfin360 C:\src\jellyfin360
}
Set-Location C:\src\jellyfin360
git log --oneline -1
(Get-ChildItem -Recurse -File | Measure-Object).Count.ToString() + ' files'
