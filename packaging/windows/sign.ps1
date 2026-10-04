# SPDX-License-Identifier: GPL-3.0-or-later
# Signs a Windows executable with Authenticode (signtool, from the Windows SDK) and a certificate in a .pfx file, adds an
# RFC 3161 timestamp so the signature outlives the certificate, and verifies the result.
#
#   pwsh packaging/windows/sign.ps1 -File gaius.exe -Pfx gaius.pfx -Password <password>
#
# The release workflow calls it when the repository has the GAIUS_SIGN_PFX_BASE64 and GAIUS_SIGN_PFX_PASSWORD secrets
# (docs/RELEASING.md, "Windows signing"). A certificate whose key lives in a cloud service or on a token (Azure Artifact
# Signing, SignPath, most extended-validation certificates) is not a .pfx and is signed by that service's own step instead.
param(
    [Parameter(Mandatory = $true)][string]$File,
    [Parameter(Mandatory = $true)][string]$Pfx,
    [Parameter(Mandatory = $true)][string]$Password,
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [string]$Description = 'Gaius'
)
$ErrorActionPreference = 'Stop'

# signtool is in the Windows SDK, not on PATH: take the newest x64 one.
$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
if (-not $signtool) {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $signtool = Get-ChildItem $kits -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $signtool) { throw 'signtool.exe not found (install the Windows SDK)' }

& $signtool sign /fd SHA256 /f $Pfx /p $Password /tr $TimestampUrl /td SHA256 /d $Description $File
if ($LASTEXITCODE -ne 0) { throw "signtool sign failed ($LASTEXITCODE)" }

$signature = Get-AuthenticodeSignature $File
if (-not $signature.SignerCertificate) { throw "$File has no signature after signing" }
Write-Output ("{0}: signed by {1} ({2})" -f $File, $signature.SignerCertificate.Subject, $signature.Status)
