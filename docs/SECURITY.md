# Security policy

## Reporting a vulnerability

Please report security vulnerabilities privately using
[GitHub's private vulnerability reporting](https://github.com/iainchesworthlabs/cubeulator/security/advisories/new)
rather than opening a public issue.

## Supported versions

Cubeulator is pre-1.0 and does not yet have a stable release; fixes land on
`develop` and are included in the next tagged release.

## Scanning

This repository runs CodeQL, OSV-Scanner, Zizmor, and OpenSSF Scorecard on
every push/PR and on a weekly schedule — see [CI & dependencies](ci.md).

## Verifying build authenticity

Every released package can be traced to the commit and workflow run that built
it, so that a download can be checked against this project's pipeline rather
than trusted by its name. Two independent checks are provided; neither is
something your operating system looks at, so Windows SmartScreen and macOS
Gatekeeper will still ask (see [Cutting a release](releasing.md)).

### Build provenance (keyless)

Each package carries a
[GitHub build-provenance attestation](https://docs.github.com/actions/security-guides/using-artifact-attestations):
a SLSA statement signed through Sigstore with the release workflow's short-lived
identity, recording the repository, commit and workflow that built the file.
There is no long-lived key to steal. With the [GitHub CLI](https://cli.github.com/):

```sh
gh attestation verify Cubeulator-<version>-linux-x64.deb --repo iainchesworthlabs/cubeulator
```

A pass prints the repository, the commit and the workflow. Reject a file that
does not verify.

### GPG signature (the maintainer's key)

Each release also holds `SHA512SUMS`, its detached signature `SHA512SUMS.asc`,
a signature beside every package and the SBOM, and the public key
`cubeulator-signing-key.asc`:

```sh
gpg --import cubeulator-signing-key.asc
gpg --verify SHA512SUMS.asc SHA512SUMS && sha512sum -c SHA512SUMS
# or one file:
gpg --verify Cubeulator-<version>-linux-x64.deb.asc Cubeulator-<version>-linux-x64.deb
```

Check the key's fingerprint against the one published here before trusting it:
the key in a release proves nothing about who made the release if it is the only
place the fingerprint appears.

The two answer different questions. Provenance ties the bytes to *this
repository and build*; the signature ties them to *the maintainer's key*.
