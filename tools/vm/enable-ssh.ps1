# One-off bootstrap for the XDK build VM: OpenSSH Server with the Mac's key, PowerShell as the shell.
$ErrorActionPreference = 'Continue'
Start-Transcript -Path \\host.lan\Data\setup.log -Force | Out-Null
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Set-Service sshd -StartupType Automatic
Start-Service sshd
New-Item -Path HKLM:\SOFTWARE\OpenSSH -Force | Out-Null
Set-ItemProperty HKLM:\SOFTWARE\OpenSSH -Name DefaultShell -Value C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe
Copy-Item \\host.lan\Data\id.pub C:\ProgramData\ssh\administrators_authorized_keys -Force
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "Administrators:F" /grant "SYSTEM:F"
if (-not (Get-NetFirewallRule -Name sshd -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name sshd -DisplayName 'OpenSSH Server' -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
}
Restart-Service sshd
Get-Service sshd | Format-List Name, Status, StartType
# Long-running installs over SSH must not be killed by sleep or screen lock.
powercfg /change standby-timeout-ac 0
powercfg /change monitor-timeout-ac 0
Stop-Transcript | Out-Null
