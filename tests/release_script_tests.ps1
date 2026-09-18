$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$releaseScript = Join-Path $repoRoot "scripts/release.ps1"
$parseErrors = $null
$tokens = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($releaseScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw "Release script parse failed: $parseErrors" }

# Import only these function definitions; never execute packaging or publishing.
foreach ($name in @("Invoke-GitHubJson", "Assert-ReleaseNotesMatch", "Get-ReleaseNotes")) {
    $definition = $ast.EndBlock.Statements | Where-Object {
        $_ -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -eq $name
    }
    if (-not $definition) { throw "Missing release helper: $name" }
    Invoke-Expression $definition.Extent.Text
}

function Invoke-RestMethod {
    param($Method, $Uri, $Headers, $ContentType, $Body)
    if ($Body -isnot [byte[]]) { throw "GitHub JSON must be sent as explicit UTF-8 bytes." }
    if ($ContentType -ne "application/json; charset=utf-8") { throw "Incorrect JSON charset." }
    $decoder = New-Object System.Text.UTF8Encoding($false, $true)
    return ($decoder.GetString($Body) | ConvertFrom-Json)
}

$notesPath = Join-Path $repoRoot "docs/release_notes_v0.2.3.md"
$notes = Get-ReleaseNotes -Version "v0.2.3" -ReleaseNotesPath $notesPath
if ($notes -notmatch '[\u4e00-\u9fff]') { throw "Test fixture must contain Chinese text." }
$result = Invoke-GitHubJson -Method Post -Uri "https://example.invalid" -Headers @{} -Body @{body=$notes;name="v0.2.3"}
Assert-ReleaseNotesMatch -Expected $notes -Actual $result.body -Platform "mock"
Assert-ReleaseNotesMatch -Expected $notes -Actual ($notes.Replace("`n", "`r`n")) -Platform "mock"
$rejected = $false
try {
    Assert-ReleaseNotesMatch -Expected $notes -Actual ($notes -replace '[\u4e00-\u9fff]', '?') -Platform "mock"
} catch { $rejected = $true }
if (-not $rejected) { throw "Corrupted release notes were accepted." }
Write-Output "Release notes UTF-8 transport and read-back checks passed."
