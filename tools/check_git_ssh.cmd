@echo off
rem Double-click me: checks that git can fetch and push TOMS over SSH on this PC
rem (ssh program, TortoiseGit plink, key, GitHub login, remote, fetch, push --dry-run, identity)
rem and offers to fix what is wrong. See docs/12_SSH_KEY_SETUP.md.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check_git_ssh.ps1" %*
set RC=%ERRORLEVEL%
pause
exit /b %RC%
