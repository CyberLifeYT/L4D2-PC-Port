@echo off
rem Builds dist\l4d2-port-patch-<version>.zip and publishes it as a full GitHub Release (v<version>).
rem Usage: scripts\release_patch.bat <version> ["release notes"]
rem Login: GITHUB_TOKEN / GH_TOKEN, else the GitHub CLI (gh auth login), else Git's credential manager.
call "%~dp0publish_common.bat" release %*
