# Windows code signing: options and recommendation

> **Parked (Andy, 2026-10-30): no budget for signing, so nothing here is being pursued.** Kept as reference in case that changes.

Research note, 2026-10-30 (laptop, review-only). Facts were checked against the sources listed at the end; anything not confirmed from a primary source is marked **unverified**. Prices move: re-check before buying.

## Why

Unsigned `.exe` files trigger SmartScreen ("Windows protected your PC"). On the employer laptop, the Defender ASR rule "Block executable files from running unless they meet a prevalence, age, or trusted list criterion" blocked `mu-On.exe`. Signing every shipped binary (standalone exes, VST3 dlls, the installer) is the first step; it does not by itself end the warnings (see section 2).

## 1. Options compared

| Option | Eligible as UK individual / sole trader? | Cost (verified?) | Key storage and CI | Notes |
|---|---|---|---|---|
| **Microsoft Artifact Signing** (formerly Trusted Signing) | **Individuals: no.** Microsoft states individual developers must be in the US or Canada. UK **organizations** are eligible. A UK sole trader counts as an individual in the unofficial Q&A answer; whether a trading name ("DBA") passes organization validation in the UK is **unverified** | Basic about $9.99/month, 5,000 signatures (Microsoft docs quote "starts at $9.99/month"; the pricing page itself showed no figures when fetched, so the figure is secondary) | Cloud, FIPS 140-3 L3, no token. Official `azure/artifact-signing-action@v2`, OIDC login, runners `windows-2022` / `windows-2025`. Needs a paid Azure subscription (free/trial not allowed) | Validity: short-lived certs managed by the service (exact lifetime **unverified** here). Organization validation takes 1 to 20 business days; a three-year tax history requirement was reported by a secondary source (**unverified**). No EV, none planned |
| **Certum** Standard Code Signing (OV), cloud (SimplySign) | Yes: certificate can carry natural-person data, so no company needed | Shop page "from EUR 139" (certum.eu, 2026-10-30; validity option not itemised). A reseller figure of about USD 348 for 3 years is **unverified** | Cloud key via SimplySign: designed for interactive use (a desktop app emulating a smart card plus a one-time code). No official CI support found. Unofficial tool `ssign` automates the TOTP seed in CI (0.1.x, not endorsed by Certum; EXE proven, MSI not yet) | Cheapest realistic OV. The CI path is the weak point. Certum also sells an "Open Source" certificate: only for genuine open-source projects, and a secondary source says it lacks Microsoft cross-certification (**unverified**), so not suitable |
| **SignPath Foundation** | Only for open-source projects: OSI licence, public repo, no proprietary components, automated build from the repo | Free | Cloud, GitHub App + Actions | **Not applicable**: mu-family is not open source |
| **SSL.com eSigner** (OV or individual-validation) | Individual validation exists (IV); exact UK sole-trader acceptance **unverified** | Certificate price **unverified** (an affiliate listing showed about USD 64.50 to 449 per year, unreliable); eSigner credentials listed at USD 20/month for extra credentials in SSL.com's guide; 30-day free eSigner trial | Cloud HSM, built for API / CI automation (CodeSignTool and a documented GitHub Actions integration exist; not opened in detail) | Most CI-friendly commercial route; likely dearer than Certum |
| **DigiCert / Sectigo** OV or EV | Mostly organization-oriented | Typically several hundred USD per year (**unverified**) | Cloud signing offered | EV no longer buys SmartScreen advantage, so not worth the premium |

### Rules that apply now (CA/Browser Forum)

- Since 2023-06-01, OV/IV code signing private keys must live in FIPS-validated hardware or a cloud HSM; no `.pfx` file you download. That is why every option above is a token or cloud service.
- Certificates issued on or after **2026-03-01** are limited to **460 days** (CA/B Forum text). Vendors state slightly lower figures (Certum 459 days from 2026-02-27, DigiCert 459, SSL.com 458). Multi-year products are re-issued each year, so expect an annual renewal step.
- Always timestamp signatures (RFC 3161), so signed files stay valid after the certificate expires.

## 2. Does signing satisfy SmartScreen and the ASR rule?

**SmartScreen (known).** Microsoft's guidance (updated 2026-08-17) says a valid OV or EV certificate still shows a warning on first download until reputation builds; it displays the verified publisher name instead of "Unknown publisher". EV no longer bypasses SmartScreen. Reputation attaches to the file hash and to the signing certificate/publisher identity; for signed files it can carry over to new versions signed by the same identity, whereas unsigned files start at zero every build. No threshold is published; Microsoft says it can take several weeks and hundreds of clean installs. There is no manual submission route for consumer SmartScreen. Some vendor pages still advertise "instant" EV reputation; Microsoft's page contradicts them. Changing certificate or publisher name resets the trust signal, so choose a stable identity once. Renewed certificates carrying reputation is reported by a secondary source (**unverified**).

**Defender ASR prevalence/age/trusted-list rule (partly unknown).** Microsoft's rule reference (updated 2026-09-11) says only that it blocks executables not meeting prevalence, age or trusted-list criteria, and that it needs cloud-delivered protection. It does **not** state that a valid signature exempts a file or what the thresholds are. The well-known older description (prevalence about 0.1 percent of enterprise endpoints, age 24 hours or more, trusted list) is from memory and **unverified**. Expected behaviour, but unproven: signing helps because reputation can accumulate on the publisher and persist across builds, but a small audience product will still be "low prevalence" for a long time. Signing is necessary but may not be sufficient for the employer laptop. Practical routes on that laptop: ask IT for a per-file or publisher allow (ASR exclusion or an indicator of compromise "Allow"), or run builds from an excluded folder.

**Smart App Control (Windows 11)** blocks unsigned files without good reputation, so signing also helps users who have it on.

## 3. Recommendation

1. **Eligibility check first.** Artifact Signing is the cheapest (about USD 10/month, about GBP 90 to 100 per year, **unverified conversion**) and best-integrated, but as a UK individual or plain sole trader Andy likely cannot use it. Do not pay for an Azure setup before confirming that a UK sole trader trading name can pass organization validation (ask Microsoft Q&A or support; **unverified**). If Andy forms a UK limited company (Companies House registration, free to browse, about GBP 50 to register), it becomes eligible, subject to the tax-history point above.
2. **If it is not eligible (the likely case): Certum Standard Code Signing in the Cloud (OV), bought as an individual**, expected about EUR 139 to 150 per year, plus a pipeline caveat. Because SimplySign is designed for interactive login, plan one of: (a) sign on the build PC with the SimplySign app logged in, as a post-step on the artifacts the workflow produces; or (b) automate with the third-party `ssign` TOTP tool in a protected GitHub Environment (extra risk: the TOTP seed is a long-lived secret). Option (a) keeps the secret off GitHub and is the lower-risk start.
3. **If CI must be fully hands-off:** SSL.com eSigner (CodeSignTool, documented GitHub Actions integration). Price must be confirmed with SSL.com; budget more than Certum. Start with their 30-day free eSigner period to prove the pipeline before paying.
4. **Budget:** about GBP 100 to 150 per year (Certum) or about GBP 90 to 100 per year (Artifact Signing, if eligible); SSL.com to be quoted.

### Identity documents (typical)

- Government photo ID (passport or UK driving licence); Artifact Signing individual validation uses a phone selfie/ID check via Microsoft Authenticator.
- Proof of address dated within the last three months (utility bill or bank statement) matching the name and address on the application.
- For an organization route: company registration details (Companies House number), a domain-based email, a business website.

### How signing slots into `release.yml`

Describe-only, no edit made. After each product is built and before it is zipped or fed to the installer builder:

1. Sign the binaries first (`.exe` standalones and the `.vst3` plugin dll(s) inside the bundle), then build the installer, then sign the installer exe last. Never alter a file after signing.
2. Artifact Signing variant: a job-level `permissions: id-token: write`; step `azure/login@v3` using secrets `AZURE_CLIENT_ID`, `AZURE_TENANT_ID`, `AZURE_SUBSCRIPTION_ID` (federated credential, no stored password); step `azure/artifact-signing-action@v2` with `endpoint`, signing account name, certificate profile name, `files-folder`, `files-folder-filter: exe,dll,vst3`, `files-folder-recurse: true`, SHA-256 digest, and a timestamp URL. The service principal needs the "Artifact Signing Certificate Profile Signer" role.
3. SSL.com / Certum variant: a Windows-runner step running CodeSignTool or `osslsigncode`/`signtool` with secrets for the account username, password, credential ID and TOTP secret, held in a GitHub Environment with required reviewer so the secret is only exposed to approved runs.
4. A verification step: `signtool verify /pa /v` on each output, failing the job on an unsigned file.
5. The Linux and macOS jobs are unaffected (macOS stays on hold).

The CLAUDE.md rule that workflows are `workflow_dispatch`-only is unchanged by this.

## 4. What Andy must decide or provide

1. Sole trader as an individual, or form a limited company to unlock Artifact Signing (cheaper, better CI)?
2. The publisher name on the certificate: personal legal name or a product/brand name. It must match the ID and is shown to users and is hard to change without resetting reputation.
3. Whether to ask the employer's IT for an ASR allow for the testing laptop, since signing may not suffice there.
4. Which CI approach he accepts: sign on the build PC after the workflow (simplest, secret-free) or fully automated in Actions (needs secrets and an Environment).
5. Budget approval for about GBP 100 to 150 per year, and the personal documents above (ID, proof of address) plus a phone for the identity check.
6. If Artifact Signing is pursued: a paid Azure subscription (card needed) and an Entra tenant.

## Sources (fetched 2026-10-30)

- Microsoft Learn, Artifact Signing quickstart (page updated 2026-10-08): eligibility by country, validation time 1 to 20 business days, accepted ID. https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart
- Microsoft Learn, Artifact Signing FAQ (updated 2026-10-27): no free or trial subscriptions, no EV, SmartScreen reputation builds over time, FIPS 140-3 L3. https://learn.microsoft.com/en-us/azure/artifact-signing/faq
- Microsoft Learn, SmartScreen reputation for Windows app developers (updated 2026-08-17): EV no longer bypasses SmartScreen, Artifact Signing "starts at $9.99/month". https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- Azure pricing page (quota 5,000 / 100,000; amounts not displayed when fetched). https://azure.microsoft.com/pricing/details/artifact-signing/
- GitHub, azure/artifact-signing-action v2. https://github.com/Azure/artifact-signing-action
- Microsoft Learn, ASR rules reference (updated 2026-09-11). https://learn.microsoft.com/en-us/defender-endpoint/attack-surface-reduction-rules-reference
- Certum shop, Standard Code Signing (price from EUR 139; 459-day maximum from 2026-02-27). https://shop.certum.eu/standard-code-signing-code.html
- CA/Browser Forum code signing requirements (460 days from 2026-03-01; via search summary). https://cabforum.org/working-groups/code-signing/requirements/
- SignPath Foundation terms. https://signpath.org/terms
- SSL.com eSigner pricing and code signing guides (via search summaries, not opened in detail). https://www.ssl.com/guide/esigner-pricing-for-code-signing/
- `ssign` (unofficial Certum SimplySign CI client). https://docs.rs/ssign
- Microsoft Q&A, Irish sole trader eligibility (unofficial answer). https://learn.microsoft.com/en-us/answers/questions/5915697/can-an-irish-sole-trader-(non-incorporated)-qualif
