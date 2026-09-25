<#
.SYNOPSIS
    Runs the GitHub "Testing the MSBuild" check locally, against a clean checkout.

.DESCRIPTION
    .github/workflows/BuildTest.yml has two jobs:

      t5000   T5000/T5000.sln, from a checkout of the T5000 folder alone. It
              proves T5000 builds and passes its self-test with nothing else
              from the repository present.
      build   "T3000 - VS2019.sln", everything else. That includes
              T5000/conformance, which checks T5000 against T3000's headers,
              tables and BACnet stack, and runs those checks.

    CI spends about half of the build job's ~9 minutes installing MFC for the v143
    toolset and the .NET Framework 4.5.2 reference assemblies onto a fresh runner
    image. Both are already installed on this machine, so locally only the builds
    themselves are left to do. The t5000 job needs neither.

    What CI still adds is the CLEAN CHECKOUT: it builds what is committed, so a
    file that was never "git add"ed fails there and not in your working tree. That
    is the part worth reproducing, and it is what this script does - it builds a
    throwaway git worktree at a given ref, never the working tree itself. For the
    t5000 job it copies that checkout's T5000 folder somewhere with nothing else
    beside it, as CI's sparse checkout has nothing else beside it.

    Submodules are deliberately NOT initialised. No project in "T3000 - VS2019.sln"
    lives under, or references, T3000_CrossPlatform, PartsAndVendors or
    T3000Webview, so cloning them would cost minutes and change nothing. If that
    ever stops being true this script stops being equivalent to CI, so it re-checks
    the assumption on every run rather than quietly passing.

.EXAMPLE
    pwsh scripts/ci-local.ps1
    pwsh scripts/ci-local.ps1 -Only T5000
    pwsh scripts/ci-local.ps1 -Ref origin/master -Parallel
#>
[CmdletBinding()]
param(
    # What to build. Anything git can resolve: HEAD, a branch, a tag, a SHA.
    [string] $Ref = 'HEAD',

    # Short on purpose. MFC plus vcpkg-export-openssl produce deep paths, and this
    # build hits MAX_PATH when rooted somewhere long like a temp directory.
    [string] $WorktreePath = 'C:\t3000-ci',

    # Where the T5000 folder is copied to be built on its own.
    [string] $T5000Path = 'C:\t5000-ci',

    # Which of CI's two jobs to run. T5000 alone takes about a minute.
    [ValidateSet('All', 'T5000', 'T3000')]
    [string] $Only = 'All',

    # Adds /m. Faster, but then it is no longer the exact command CI runs.
    [switch] $Parallel,

    # Leaves the checkouts in place afterwards, to inspect or run the output.
    [switch] $Keep
)

$ErrorActionPreference = 'Stop'

function Step($text) { Write-Host "`n=== $text" -ForegroundColor Cyan }
function Ok($text)   { Write-Host "  ok    $text" -ForegroundColor DarkGreen }
function Note($text) { Write-Host "  note  $text" -ForegroundColor DarkYellow }

$started = Get-Date
$runT5000 = $Only -ne 'T3000'
$runT3000 = $Only -ne 'T5000'

$repo = & git rev-parse --show-toplevel 2>$null
if (-not $repo) { throw "Not inside a git repository." }
$repo = $repo.Replace('/', '\')

# -------------------------------------------------------------------- preflight
# The two steps CI runs before building the T3000 solution. Checked rather than
# installed: if one is missing the build dies at MSB8041 or MSB3644 with no hint
# of why, and fixing it is a Visual Studio Installer job, not something to do
# silently from a script. T5000 alone needs neither.
Step "Preflight"

if ($runT3000) {
    $mfcGlob = 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\14.[34]*\atlmfc\include\afxwin.h'
    $mfc = Get-ChildItem $mfcGlob -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $mfc) {
        Write-Error "MFC for the v143 toolset is missing. In the Visual Studio Installer add the MFC component for v143 build tools; without it the build stops at MSB8041."
    }
    Ok "MFC v143 - $($mfc.FullName)"

    $refRoot = Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies\Microsoft\Framework\.NETFramework\v4.5.2'
    if (-not (Test-Path (Join-Path $refRoot 'mscorlib.dll'))) {
        Write-Error ".NET Framework 4.5.2 reference assemblies are missing, so the managed projects will stop at MSB3644. CI installs the Microsoft.NETFramework.ReferenceAssemblies.net452 NuGet package into $refRoot - see BuildTest.yml."
    }
    Ok ".NET 4.5.2 reference assemblies"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
           Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found via vswhere." }
Ok "MSBuild - $msbuild"

# ----------------------------------------------------------------- what is built
Step "Resolving $Ref"

$sha = & git -C $repo rev-parse --verify "$Ref^{commit}" 2>$null
if (-not $sha) { throw "Cannot resolve '$Ref' to a commit." }
Ok (& git -C $repo log -1 --format='%h %s' $sha)

# The whole point of building a worktree is that uncommitted work is invisible to
# it, exactly as it is invisible to CI. Say what is being left out, so a pass is
# not misread as "my working tree builds".
$dirty = @(& git -C $repo status --porcelain)
if ($dirty.Count -gt 0) {
    Note "$($dirty.Count) uncommitted path(s) are NOT part of this build:"
    $dirty | Select-Object -First 10 | ForEach-Object { Write-Host "          $_" -ForegroundColor DarkYellow }
    if ($dirty.Count -gt 10) { Note "... and $($dirty.Count - 10) more" }
}

# ----------------------------------------------------------------- clean checkout
Step "Clean checkout at $WorktreePath"

if (Test-Path $WorktreePath) {
    & git -C $repo worktree remove --force $WorktreePath 2>$null | Out-Null
    if (Test-Path $WorktreePath) { Remove-Item $WorktreePath -Recurse -Force }
}
& git -C $repo worktree prune
# git writes its checkout progress to stderr, so both streams are muted here -
# 3800 "Updating files: n%" lines bury everything this script has to say.
& git -C $repo worktree add --detach $WorktreePath $sha 2>&1 | Out-Null
if (-not (Test-Path $WorktreePath)) { throw "git worktree add failed." }
Ok "checked out clean, no submodules"

# Re-checks the assumption that lets this script skip "submodules: recursive".
$sln = Join-Path $WorktreePath 'T3000 - VS2019.sln'
$slnText = Get-Content $sln -Raw
foreach ($sub in 'T3000_CrossPlatform', 'PartsAndVendors', 'T3000Webview') {
    if ($slnText.Contains($sub)) {
        Write-Error "The solution now references the $sub submodule, so skipping submodules no longer matches CI. Add 'git submodule update --init --recursive' in the worktree."
    }
}

# ------------------------------------------------------------------------ builds
$common = @('/p:Platform=x86', '/p:Configuration=Release', '/nologo', '/v:minimal')
if ($Parallel) {
    $common += '/m'
    Note "/m added - faster, but no longer the exact command CI runs"
}

# Not beside the checkouts: the default ones are at the root of C:\, where the
# stock Windows ACL lets Users create directories but not files. And not inside
# them either, since they are deleted before you would go read the log.
function Build($name, $solution, $extra, $log) {
    Write-Host "  log   $log"
    $t = Get-Date
    # To the screen, not down the pipeline, or every line of it would come
    # back from this function alongside the result.
    & $msbuild $solution @common @extra 2>&1 | Tee-Object -FilePath $log | Out-Host
    [pscustomobject]@{
        Name   = $name
        Code   = $LASTEXITCODE
        Log    = $log
        Mins   = [math]::Round(((Get-Date) - $t).TotalMinutes, 1)
        # The summary line each test runner prints. A build can succeed with
        # no tests at all if the post-build step is lost, so it is looked for.
        Checks = @(Select-String -Path $log -Pattern '\d+ checks, \d+ failures' | ForEach-Object { $_.Line.Trim() })
    }
}

$results = @()

if ($runT5000) {
    Step "Job t5000: T5000 on its own, at $T5000Path"
    if (Test-Path $T5000Path) { Remove-Item $T5000Path -Recurse -Force }
    New-Item -ItemType Directory $T5000Path | Out-Null
    Copy-Item (Join-Path $WorktreePath 'T5000') (Join-Path $T5000Path 'T5000') -Recurse
    Ok "only T5000\ copied, as CI checks out only T5000/"
    $results += Build 't5000' (Join-Path $T5000Path 'T5000\T5000.sln') @() (Join-Path $env:TEMP 't5000-ci.log')
}

if ($runT3000) {
    Step "Job build: T3000 - VS2019.sln"
    $results += Build 'build' $sln @('/p:ProjectVersion=20230804') (Join-Path $env:TEMP 't3000-ci.log')
}

# ------------------------------------------------------------------------ result
Step "Result"

$failed = $false
foreach ($r in $results) {
    if ($r.Code -ne 0) {
        $failed = $true
        Write-Host "  FAIL  $($r.Name) in $($r.Mins) min (exit $($r.Code)). Full log: $($r.Log)" -ForegroundColor Red
        Select-String -Path $r.Log -Pattern 'error [A-Z]+[0-9]+|  FAIL  ' | Select-Object -First 15 |
            ForEach-Object { Write-Host "          $($_.Line.Trim())" -ForegroundColor Red }
    }
    elseif ($r.Checks.Count -eq 0) {
        $failed = $true
        Write-Host "  FAIL  $($r.Name) built, but no test summary appeared. Full log: $($r.Log)" -ForegroundColor Red
    }
    else {
        Ok "$($r.Name) in $($r.Mins) min"
        $r.Checks | ForEach-Object { Write-Host "          $_" }
    }
}

if ($runT5000 -and -not $failed) {
    $exe = Join-Path $T5000Path 'T5000\bin\Release\T5000.exe'
    if (Test-Path $exe) { Ok "T5000.exe - $([math]::Round((Get-Item $exe).Length / 1MB, 1)) MB" }
}
if ($runT3000 -and -not $failed) {
    $exe = Join-Path $WorktreePath 'T3000 Output\release\T3000.exe'
    if (Test-Path $exe) { Ok "T3000.exe - $([math]::Round((Get-Item $exe).Length / 1MB, 1)) MB" }
}

if ($failed) { Write-Host "`n  FAILED" -ForegroundColor Red }
else { Write-Host "`n  PASSED" -ForegroundColor Green }

if ($Keep) {
    Note "checkouts kept at $WorktreePath and $T5000Path"
    Note "remove the worktree with: git -C `"$repo`" worktree remove --force `"$WorktreePath`""
}
else {
    & git -C $repo worktree remove --force $WorktreePath 2>$null | Out-Null
    if (Test-Path $WorktreePath) { Remove-Item $WorktreePath -Recurse -Force -ErrorAction SilentlyContinue }
    & git -C $repo worktree prune
    if (Test-Path $T5000Path) { Remove-Item $T5000Path -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host "  total $([math]::Round(((Get-Date) - $started).TotalMinutes, 1)) min"
exit ([int]$failed)
