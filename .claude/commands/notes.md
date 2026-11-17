Add user-facing release notes for work just done, **as the work happens** (Andy 2026-11-17: the In-Testing section is kept current continuously, never batched at release time), into the **"Next release · In testing"** section of each affected product's release-notes page, then commit + push the site. Use $ARGUMENTS as the description if given; otherwise derive the one-liner(s) from the changes made this session.

## Which pages

- `site/mu-clid-releases.html` and/or `site/mu-tant-releases.html` — one entry per product whose code path the change touches.
- **A shared mu-core fix goes on every product whose code path it touches** (usually both).
- Only add notes for **user-facing** changes (New / Improved / Fixed behaviour a user would notice). Skip pure-internal refactors, build tooling, docs.

## Entry rules

- Plain English, no low-level/internal detail. A user should understand it.
- End each line with the backlog item number in parens via the `ref` span.
- **Every update, in every product's release notes (In-Testing and each released version), is sorted into New / Improved / Fixed** (Andy 2026-11-17). New = a feature or option that did not exist; Improved = something that already existed and now works or sounds better; Fixed = something that was wrong. Pick the right group; create the group block only if it doesn't already exist, and leave an empty group out (no "Nothing this time" line).
- Write the note in the same session as the change, not later in a batch.
- If the In-Testing section only says "Nothing yet", replace that line with the first real entry.

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

Do **not** create a new dated `v1.2.NNN` section — that promotion happens only at release time (see `/release`).

## Finish

Commit the site files on main (by explicit path), push, then `gh workflow run publish-site.yml --ref main` so the notes go live straight away (Andy 2026-11-15: no Netlify limit any more, keep the In-Testing section always current and published).
