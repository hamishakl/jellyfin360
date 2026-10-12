# Building on a Mac or Linux box: a Windows VM for the XDK

The console build needs the Xbox 360 XDK and Visual Studio 2010, which only run on x86
Windows. This folder sets up a Windows 10 VM under KVM with [dockur/windows](https://github.com/dockur/windows)
on any Linux host with Docker, then drives it over SSH so the whole build loop runs from a
terminal on the Mac. An x86 host runs VS2010 natively; on an Apple Silicon Mac a UTM VM works too
but runs the compiler under emulation.

1. On the Linux host (needs `/dev/kvm`, ~6 GB RAM to spare and ~40 GB disk):
   ```
   mkdir -p ~/win-xdk && cp docker-compose.yml ~/win-xdk/ && cd ~/win-xdk
   # edit PASSWORD, then:
   docker compose up -d
   ```
   It downloads Windows 10 from Microsoft and installs it unattended (20-40 min). Watch at
   `http://<host>:8006`.
2. Put your SSH public key in `~/win-xdk/shared/id.pub` and `enable-ssh.ps1` in the same folder.
   In the VM's console open an admin PowerShell (right-click Start) and run
   `powershell -ep bypass -file \\host.lan\data\enable-ssh.ps1`. (The noVNC console drops the
   Shift key, so that command is deliberately all lowercase.)
3. From your machine: `ssh -p 2222 <user>@<host>` lands in PowerShell. Add an alias to
   `~/.ssh/config`. Scripts are easiest to run as `scp x.ps1 vm:C:/Users/<user>/` then
   `ssh vm 'powershell -ep bypass -file C:\Users\<user>\x.ps1'`: quotes do not survive the
   SSH command line on Windows.
4. `install-git.ps1` installs MinGit and clones this repo to `C:\src\jellyfin360`.
5. Install Visual Studio 2010 SP1, then the XDK 2.0.21256, inside the VM (drop the installers in
   `~/win-xdk/shared`, they appear as `\\host.lan\Data`). Neither is distributed here.
6. Build: `powershell -ep bypass -file C:\src\jellyfin360\tools\build.ps1`. Deploy to the console
   with `tools\run.ps1`; the VM reaches the console over the host's LAN (NAT), so set the console
   name/IP in the XDK's Xbox 360 Neighborhood first.
