# docs/ -- GitHub Pages site (Hugo)

Almost everything under this `docs/` folder (`config/`, `content/`, `layouts/`, `assets/`,
`static/`) is the Hugo site source for the project's GitHub Pages site, deployed by
`../.github/workflows/hugo.yml` on every push to `main`. Built with
[Hugo](https://gohugo.io/) and the [Stack theme](https://github.com/CaiJimmy/hugo-theme-stack),
copied from the sibling `DIY-MIDI-METRONOME.public/documentation-github-page` site (same
author, same template). The one exception is `README.md` right here, unrelated to Hugo --
it's just a short pointer to `content/format/index.md`, the actual Kronos `.PCG`/`.SNG`
file-format reference (see that file's own note on why it's not a second full copy anymore).

## Local development

Hugo (extended) and Go are required to build this site; both are pinned into the bundled
Docker image, so no local install is needed:

```bash
cd docs
docker compose up
```

Then open http://localhost:1313. Content lives under `content/`; the theme config is
under `config/_default/`.

## Structure

- `content/overview` -- project intro
- `content/me` -- author bio + legal notice (Impressum/Datenschutzerklärung), reused
  verbatim (same author) from the sibling DIY project sites

## One-time repo setup

GitHub Pages needs to be switched to "GitHub Actions" as its source under this repo's
Settings > Pages -- this hasn't been done yet and isn't something this tool can flip on
its own.
