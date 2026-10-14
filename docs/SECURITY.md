# Security policy

## Reporting a vulnerability

Please report security vulnerabilities **privately**, not as a public issue or
pull request, through
[GitHub's private vulnerability reporting](https://github.com/iainchesworthlabs/cubeulator/security/advisories/new):
it opens a private advisory that only you and the maintainer can see.

What helps most:

- what is affected: the version (**Settings > About Cubeulator**, or the name of
  the package), the platform, and whether it is the desktop app, the browser
  build or a release file;
- how to reproduce it, or what you found;
- what someone could do with it.

Please do not attach a camera picture that shows a person or a room.

If you cannot use the form (you have no GitHub account, say), open an issue that
says only that you have a security matter, with no details, and the maintainer
will reply there to arrange a private channel.

### What counts

Anything that breaks what the app says about itself is in scope: a camera
picture saved, sent or exposed without someone switching a developer option on
(see [Privacy](guide/privacy.md)), a release file that does not verify against
its signature or attestation (see
[Verifying build authenticity](#verifying-build-authenticity)), a way to run
code through a crafted camera frame, settings file or history file, or a flaw
in the build and release workflows.

### What to expect

Cubeulator is maintained by one person, so there is no guaranteed response
time. A report is read and acknowledged in the advisory; one that is confirmed
is fixed on `develop` and published with the next tagged release. Say in the
report if you would like to be credited, and please allow time for a fix to
ship before you publish details: the advisory is where that is agreed.

## Supported versions

Cubeulator is pre-1.0 and every release so far is a pre-release
(`0.1.0-beta.N`). Only the newest release is supported: a fix lands on
`develop` and ships in the next tagged release, and an older beta is not
patched.

## Scanning

This repository runs CodeQL, OSV-Scanner and Zizmor on every push and pull
request to `develop` and `main` and on a weekly schedule, and OpenSSF Scorecard
on pushes to `main` and weekly — see [CI & dependencies](ci.md).

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
gpg --verify SHA512SUMS.asc SHA512SUMS && sha512sum -c --ignore-missing SHA512SUMS
# or one file:
gpg --verify Cubeulator-<version>-linux-x64.deb.asc Cubeulator-<version>-linux-x64.deb
```

Check the key's fingerprint against the one published here before trusting it:
the key in a release proves nothing about who made the release if it is the only
place the fingerprint appears.

| | |
|---|---|
| Key | `Cubeulator Release Signing Key`, RSA 4096, signing only |
| Fingerprint | `0CF2 A242 440A 7AF0 18A2  BFF3 03E2 D330 B299 EB46` |
| Created | 2026-10-14 |
| Expires | 2031-10-13 |

```sh
gpg --show-keys --fingerprint cubeulator-signing-key.asc
```

`gpg --verify` also prints the signing key's `Primary key fingerprint`, and
warns that the key is not certified by anyone you trust. That warning is
expected: the fingerprint above is what to compare.

The two answer different questions. Provenance ties the bytes to *this
repository and build*; the signature ties them to *the maintainer's key*.
