# docs/ -- GitHub Pages site (Hugo)

Almost everything under this `docs/` folder (`config/`, `content/`, `static/`) is the Hugo
site source for the project's GitHub Pages site, deployed by `../.github/workflows/
hugo.yml` on every push to `main`. Built with [Hugo](https://gohugo.io/) and the
[Stack theme](https://github.com/CaiJimmy/hugo-theme-stack). The one exception is
`README.md` right here, unrelated to Hugo -- it's just a short pointer to
`content/format/index.md`, the actual Kronos `.PCG`/`.SNG` file-format reference (see that
file's own note on why it's not a second full copy anymore).

**No local `layouts/`/`assets/scss/`/`assets/icons/` here anymore (2026-08-09)** -- those
used to be a hand-copied duplicate of the exact same files in the sibling
`DIY-MIDI-METRONOME.public`/`DIY-PEDALBOARD.public` sites, which had already drifted (a
real local-dev bugfix here was still missing there). Now imported as a real
[Hugo Module](https://gohugo.io/hugo-modules/use-modules/) from
[github.com/jens-goes-mad/DIY-HUGO-SCAFFOLD.public](https://github.com/jens-goes-mad/DIY-HUGO-SCAFFOLD.public)
-- see `config/_default/module.toml`'s own comment for the (counterintuitive, verified-
not-assumed) import-order requirement, and that repo's own README for what's shared this
way vs. what has to stay a per-site config file (`config.toml`/`params.toml`/`menu.toml`,
plus `docker-compose.yml`, none of which Hugo Modules can inject into a consuming site).

## Local development

Hugo (extended) and Go are required to build this site; both are pinned into the bundled
Docker image, so no local install is needed:

```bash
cd docs
docker compose up
```

Then open http://localhost:1313/DIY-KORG-KRONOS-EDITOR.public/ (note the subpath -- `docker-
compose.yml`'s `--baseURL` override matches the real GitHub Pages project-page path so
locally-served links/assets resolve the same way they will in production, see that file's
own comment for why this matters). Content lives under `content/`; the theme config is
under `config/_default/`.

If Docker isn't available (or you just want one production-equivalent build rather than a
live-reloading dev server), a one-shot `hugo --minify` run works the same way without
`docker compose up`'s long-running server:

```bash
cd docs
docker run --rm -v "$(pwd):/src" -w /src hugomods/hugo:exts-non-root hugo --minify
```

This writes straight to `docs/public/` (gitignored) -- open the generated `.html` files
directly, or point any static file server at that directory.

## Structure

- `content/overview` -- project intro
- `content/guide` -- the User Guide (an overview page plus one sub-page per main working
  area: `setlist/`, `combi/`, `prog/`)
- `content/me` -- author bio + legal notice (Impressum/Datenschutzerklärung), reused
  verbatim (same author) from the sibling DIY project sites
