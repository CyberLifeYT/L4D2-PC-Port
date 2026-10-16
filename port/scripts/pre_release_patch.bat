@echo off
rem Builds dist\l4d2-port-patch-<version>.zip and publishes it as a GitHub *pre-release*.
rem Players on the launcher's "Pre-release" channel get it; "Release" players do not.
rem Usage: scripts\pre_release_patch.bat <version> ["release notes"]   e.g. 1.1.0-beta.1
call "%~dp0publish_common.bat" prerelease %*
