# Open-Source Pre-Release Checklist

Use this checklist before publishing the repository or a release branch.

## Branding sweep

- [ ] Search for product and company identifiers such as brand names, logo
  mentions, internal contact lines, and footer signatures.
- [ ] Replace or remove identifiers that are not meant for the public
  repository.
- [ ] Check comments, scripts, docs, templates, generated assets, and startup
  assets for lingering private wording.

## Placeholder review

- [ ] Replace temporary names, URLs, emails, and internal paths with neutral
  public equivalents.
- [ ] Verify sample configs and README snippets do not point to private
  services, accounts, or devices.
- [ ] Make sure examples stay valid after redaction.

## Export-safe defaults

- [ ] Keep secrets, API keys, device credentials, and personal contact details
  out of committed defaults.
- [ ] Confirm that optional network or remote-service features remain disabled
  until explicitly configured.
- [ ] Keep any recovery, provisioning, or destructive flows explicit and
  user-authorized.

## Verification

- [ ] Run `scripts/verify_pairing.py`.
- [ ] Run `scripts/verify_snapshot_integrity.py`.
- [ ] Run `scripts/verify_public_branding.py`.
- [ ] Review the final diff for public-facing text and accidental private data.
- [ ] Confirm `git diff --check` is clean.
