Add user-facing release notes for work just done, into the **"Next release · In testing"** section of each affected product's release-notes page, then commit + push the site. Use $ARGUMENTS as the description if given; otherwise derive the one-liner(s) from the changes made this session.

## Which pages

- `site/mu-clid-releases.html` and/or `site/mu-tant-releases.html` — one entry per product whose code path the change touches.
- **A shared mu-core fix goes on every product whose code path it touches** (usually both).
- Only add notes for **user-facing** changes (New / Improved / Fixed behaviour a tester would notice). Skip pure-internal refactors, build tooling, docs.

## Entry rules

- Plain English, no low-level/internal detail. A tester should understand it.
- End each line with the backlog item number in parens via the `ref` span.
- Group under **New / Improved / Fixed** — pick the right group; create the group block only if it doesn't already exist in the In-Testing section.

## Where to insert (HTML structure)

The In-Testing section starts at the `<!-- ── Next release · In testing ── -->` comment. Inside it are up to three `<div class="rn-group ...">` blocks, each with an `<h3>` and a `<ul>`. Prepend the new `<li>` to the top of the matching group's `<ul>`:

```html
<li>Fixed a rare crash when hot-swapping a rhythm. <span class="ref">(995)</span></li>
```

Group block shape (if you must create one — match the existing order New → Improved → Fixed):

```html
<div class="rn-group fixed">
  <h3>Fixed</h3>
  <ul>
    <li>… <span class="ref">(NNN)</span></li>
  </ul>
</div>
```

Do **not** create a new dated `v1.0.NNN` section — that promotion happens only at release time (see `/release`).

## Finish

Commit + push the site (site-only → Netlify auto-deploys, no CI). The In-Testing section is public, so it must always be current and pushed live.
