<#
.SYNOPSIS
    Checks, on loopback, that a client which stops part way through a request
    holds up T5000's server for no longer than the request budget.

.DESCRIPTION
    T5000's HTTP server reads one connection at a time, so a client that
    connects and then stalls - part of a head, a body shorter than its
    Content-Length, a byte at a time, or nothing at all - holds up every other
    request behind it. Each request has http::kRequestBudgetMs (5 s), from when
    its connection is accepted, to arrive in full.

    The self-test (T5000/http/server_selftest.cpp) drives one connection at a
    time through Server::answer, with a short budget. This checks the running
    server instead: that a request behind a stalled client is answered once the
    stalled one's budget is spent; that serve_forever gives each connection the
    whole budget; that another site's page is refused on its head, without the
    server waiting for the body it declares; and that the largest body T5000
    takes, 16 MiB to the Firmware page's check, arrives well inside the budget.

    It starts T5000.exe --no-browser with a scratch --db on 127.0.0.1:8730, so
    nothing else may be listening there, and stops it afterwards. Nothing is
    sent to any device. It takes about 25 seconds.

.EXAMPLE
    pwsh -NoProfile -File T5000\tools\check_request_budget.ps1

.EXAMPLE
    pwsh -NoProfile -File T5000\tools\check_request_budget.ps1 -Exe C:\somewhere\T5000.exe
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\bin\Release\T5000.exe'),
    [int]$BudgetMs = 5000
)

$ErrorActionPreference = 'Stop'

$port  = 8730
$ascii = [Text.Encoding]::ASCII
$host_ = "Host: 127.0.0.1:$port`r`n"
$own   = "${host_}Origin: http://127.0.0.1:$port`r`n"

# For a slow machine. Each failure this looks for is out by the whole budget.
$slackMs = 2000

$script:checks   = 0
$script:failures = 0

function Check([bool]$ok, [string]$what, [string]$detail)
{
    $script:checks++
    if ($ok)
    {
        Write-Host "  ok    $what"
        return
    }
    $script:failures++
    Write-Host "  FAIL  $what" -ForegroundColor Red
    if ($detail) { Write-Host "        $detail" -ForegroundColor Red }
}

function Open-Client
{
    $client = [Net.Sockets.TcpClient]::new()
    $client.NoDelay = $true
    $client.Connect('127.0.0.1', $port)
    return $client
}

function Send-Text($client, [string]$text)
{
    $bytes = $ascii.GetBytes($text)
    $client.GetStream().Write($bytes, 0, $bytes.Length)
}

# All the server sends before it closes the connection, or before $waitMs
# pass with nothing more.
function Read-Reply($client, [int]$waitMs)
{
    $stream = $client.GetStream()
    $stream.ReadTimeout = $waitMs
    $buffer = [byte[]]::new(8192)
    $reply  = [Text.StringBuilder]::new()
    try
    {
        while (($n = $stream.Read($buffer, 0, $buffer.Length)) -gt 0)
        {
            [void]$reply.Append($ascii.GetString($buffer, 0, $n))
        }
    }
    catch [IO.IOException]
    {
        # The wait ran out, or the server reset the connection.
    }
    return $reply.ToString()
}

# The status a reply starts with, or 'nothing'.
function Status([string]$reply)
{
    if ($reply -match '^HTTP/1\.1 (\d{3}) ') { return $Matches[1] }
    if ($reply -eq '') { return 'nothing' }
    return 'something that is not HTTP'
}

# Sends a whole GET / on a new connection, and returns its reply and how long
# it took to come back.
function Get-Page([int]$waitMs)
{
    $clock  = [Diagnostics.Stopwatch]::StartNew()
    $client = Open-Client
    Send-Text $client "GET / HTTP/1.1`r`n$host_`r`n"
    $reply = Read-Reply $client $waitMs
    $client.Dispose()
    return [pscustomobject]@{ Status = (Status $reply); Ms = $clock.ElapsedMilliseconds }
}

$Exe = (Resolve-Path $Exe).Path

$listening = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
if ($listening)
{
    throw "Something is already listening on port $port (pid $($listening[0].OwningProcess)). " +
          'Stop it first: this check starts its own T5000 there.'
}

$db = Join-Path ([IO.Path]::GetTempPath()) ('t5000-budget-check-' + [Guid]::NewGuid().ToString('N') + '.db')
Write-Host "T5000   $Exe"
Write-Host "Db      $db"
Write-Host "Budget  $BudgetMs ms"
Write-Host ''

$t5000 = Start-Process -FilePath $Exe -ArgumentList @('--no-browser', '--db', "`"$db`"") -PassThru -WindowStyle Hidden
try
{
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue))
    {
        if ($t5000.HasExited) { throw "T5000 exited with code $($t5000.ExitCode) before it listened." }
        if ($clock.ElapsedMilliseconds -gt 15000) { throw "T5000 did not listen on port $port within 15 s." }
        Start-Sleep -Milliseconds 100
    }

    $wait = $BudgetMs + $slackMs + 5000

    Write-Host 'A request from nothing stalled'
    $page = Get-Page $wait
    Check ($page.Status -eq '200') 'GET / is answered 200' "got $($page.Status)"
    Check ($page.Ms -lt 1000) 'at once' "took $($page.Ms) ms"

    Write-Host ''
    Write-Host 'A client that stops part way through its head'
    $stalled = Open-Client
    Send-Text $stalled "GET / HTTP/1.1`r`n$host_"
    Start-Sleep -Milliseconds 200
    $page = Get-Page $wait
    Check ($page.Status -eq '200') 'the next request is answered 200' "got $($page.Status)"
    Check ($page.Ms -lt $BudgetMs + $slackMs) "when the stalled one's budget is spent, not whenever it lets go" "took $($page.Ms) ms"
    Check ($page.Ms -ge $BudgetMs - 1000) 'and not before: it waited behind the stalled one for the whole budget' "took $($page.Ms) ms"
    $status = Status (Read-Reply $stalled 2000)
    $stalled.Dispose()
    Check ($status -eq '400') 'the stalled client is answered 400' "got $status"

    Write-Host ''
    Write-Host 'A connection that sends nothing'
    $idle = Open-Client
    Start-Sleep -Milliseconds 200
    $page = Get-Page $wait
    Check ($page.Status -eq '200') 'the next request is answered 200' "got $($page.Status)"
    Check ($page.Ms -lt $BudgetMs + $slackMs) "when the idle one's budget is spent" "took $($page.Ms) ms"
    $status = Status (Read-Reply $idle 2000)
    $idle.Dispose()
    Check ($status -eq 'nothing') 'the idle connection is closed without an answer: it asked nothing' "got $status"

    Write-Host ''
    Write-Host 'A body shorter than its Content-Length'
    $clock  = [Diagnostics.Stopwatch]::StartNew()
    $client = Open-Client
    Send-Text $client "POST /api/nothing-here HTTP/1.1`r`n${own}Content-Length: 10`r`n`r`nabc"
    $status = Status (Read-Reply $client $wait)
    $ms = $clock.ElapsedMilliseconds
    $client.Dispose()
    Check ($status -eq '400') 'is answered 400, not handed to the routes (which would answer 404)' "got $status"
    Check ($ms -ge $BudgetMs - 1000 -and $ms -lt $BudgetMs + $slackMs) 'when the budget is spent' "took $ms ms"

    Write-Host ''
    Write-Host 'A client that sends a byte at a time'
    $clock  = [Diagnostics.Stopwatch]::StartNew()
    $client = Open-Client
    Send-Text $client "GET / HTTP/1.1`r`nX-Slow: "
    while ($clock.ElapsedMilliseconds -lt 2 * $BudgetMs + $slackMs)
    {
        # Readable once the server has answered or closed the connection.
        if ($client.Client.Poll(0, [Net.Sockets.SelectMode]::SelectRead)) { break }
        try { Send-Text $client 'a' } catch { break }
        Start-Sleep -Milliseconds 250
    }
    $ms = $clock.ElapsedMilliseconds
    $client.Dispose()
    Check ($ms -lt $BudgetMs + $slackMs) 'is given up on when the budget is spent, not given more with each byte' "took $ms ms"

    Write-Host ''
    Write-Host "Another site's page, declaring a body it never sends"
    $clock  = [Diagnostics.Stopwatch]::StartNew()
    $client = Open-Client
    Send-Text $client ("POST /api/devices/forget HTTP/1.1`r`n$host_" +
                       "Origin: https://evil.example`r`nContent-Length: 100`r`n`r`n")
    $status = Status (Read-Reply $client $wait)
    $ms = $clock.ElapsedMilliseconds
    $client.Dispose()
    Check ($status -eq '403') 'is refused 403' "got $status"
    Check ($ms -lt 1000) 'at once, on its head, without waiting for the body' "took $ms ms"

    Write-Host ''
    Write-Host "The largest body T5000 takes: 16 MiB to the Firmware page's check"
    $size   = 16 * 1024 * 1024
    $clock  = [Diagnostics.Stopwatch]::StartNew()
    $client = Open-Client
    Send-Text $client ("POST /api/firmware/check?handle=1&name=budget.bin HTTP/1.1`r`n${own}" +
                       "Content-Length: $size`r`n`r`n")
    $client.GetStream().Write([byte[]]::new($size), 0, $size)
    $reply = Read-Reply $client $wait
    $ms = $clock.ElapsedMilliseconds
    $client.Dispose()
    $blank = $reply.IndexOf("`r`n`r`n")
    $body  = if ($blank -ge 0) { $reply.Substring($blank + 4) } else { '' }
    # A body cut short by the budget is answered 400 "Bad request"; the route
    # answers anything else.
    Check ((Status $reply) -ne 'nothing' -and $body -ne 'Bad request') 'arrives whole, and reaches the route' "got $(Status $reply): $body"
    Check ($ms -lt $BudgetMs / 2) 'well inside the budget' "took $ms ms"
}
finally
{
    if (-not $t5000.HasExited)
    {
        Stop-Process -Id $t5000.Id -Force
        $t5000.WaitForExit()
    }
    Remove-Item -LiteralPath $db, "$db-wal", "$db-shm" -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "$($script:checks) checks, $($script:failures) failures"
exit ([int]($script:failures -gt 0))
