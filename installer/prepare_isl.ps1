# Converts a language file for Inno Setup 5.
#
# Inno Setup 5.6 reads .isl files with the ANSI code page: a UTF-8 file (with or
# without BOM) would end up as mojibake inside the produced setup program, and a
# BOM in front of the first line even aborts the compile with
# "Text is not inside a section".
#
# So the original translation is left untouched in the repository and a
# converted copy (code page 936 for Chinese) is written next to the compiler
# output, which is then handed to ISCC via /DZH_ISL_FILE=.
#
# Called by build_installer.bat.
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Target,
    [int]$CodePage = 936
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Source)) {
    Write-Output ("[WARN] language file not found: {0}" -f $Source)
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($Source)
$hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
$offset = if ($hasBom) { 3 } else { 0 }

$ansi = [System.Text.Encoding]::GetEncoding($CodePage)
$strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
$text = $null
$isUtf8 = $false
try {
    $text = $strictUtf8.GetString($bytes, $offset, $bytes.Length - $offset)
    $isUtf8 = $true
} catch {
    $isUtf8 = $false
    # Already an ANSI file: read it with the code page the setup will use.
    $text = $ansi.GetString($bytes)
}

$targetDir = Split-Path $Target -Parent
if (-not (Test-Path -LiteralPath $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
}

# Inno Setup 5 is a non Unicode program: a LanguageName with real characters in
# it is read as ISO-8859-1 and shows up garbled in the language dialog.  Its own
# recommended fix is the <nnnn> escape, which is pure ASCII.
$text = [regex]::Replace($text, '(?m)^(LanguageName=)(.*)$', {
    param($match)
    $value = $match.Groups[2].Value
    $escaped = ''
    foreach ($ch in $value.ToCharArray()) {
        if ([int]$ch -gt 127) { $escaped += ('<{0:X4}>' -f [int]$ch) } else { $escaped += $ch }
    }
    $match.Groups[1].Value + $escaped
})

[System.IO.File]::WriteAllBytes($Target, $ansi.GetBytes($text))

$lost = 0
foreach ($ch in $text.ToCharArray()) {
    if ([int]$ch -gt 127 -and $ansi.GetBytes([string]$ch)[0] -eq 0x3F) { $lost++ }
}
$sourceKind = if ($isUtf8) { "UTF-8$(if ($hasBom) { ' with BOM' } else { '' })" } else { "code page $CodePage" }
Write-Output ("[INFO] {0} is {1}; wrote the code page {2} copy the compiler needs" -f `
              (Split-Path $Source -Leaf), $sourceKind, $CodePage)
if ($lost -gt 0) {
    Write-Output ("[WARN] {0} character(s) are not representable in code page {1} and became '?'" -f $lost, $CodePage)
}

exit 0
