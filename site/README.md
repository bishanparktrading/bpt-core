# `site/` — portfolio site source (MkDocs Material)

Static-site source for [bishanparktrading.github.io/bpt-core](https://bishanparktrading.github.io/bpt-core/).

## Local preview

```bash
cd site
pip install -r requirements.txt
mkdocs serve     # http://127.0.0.1:8000 with live-reload
```

## Build

```bash
cd site
mkdocs build --strict --site-dir _build
```

Output lands in `site/_build/` (gitignored).

## Deploy

Auto-deploys on push to `main` when files under `site/**` change. See
`.github/workflows/docs.yml`. The workflow builds + uploads as a Pages
artifact + deploys via the GitHub Pages action.

**One-time GitHub Pages setup** (operator, in repo Settings):
1. Settings → Pages → Source: **GitHub Actions**
2. (No branch / folder needed — Pages reads the workflow artifact)

After the first successful deploy, the site is live at the URL configured
in `mkdocs.yml`'s `site_url`.

## Layout

```
site/
├── mkdocs.yml          # config — nav, theme, markdown extensions
├── requirements.txt    # MkDocs + plugins versions (pinned)
├── docs/
│   ├── index.md        # landing
│   ├── architecture.md
│   ├── services/       # one page per service
│   ├── decisions/      # technical choice + reasoning per page
│   ├── ops/            # monitoring, risk, deployment
│   └── stylesheets/extra.css
└── README.md           # this file
```

## Adding a page

1. Drop a `.md` file under the right `docs/<section>/` dir.
2. Add it to the `nav:` block in `mkdocs.yml`.
3. Push — CI rebuilds automatically.

## Conventions

- Numbers > adjectives. "BBO decode 50ns" beats "low-latency."
- Link to actual code (commit URLs, file paths in the repo) so readers can dig.
- Keep deep-dives at ~500-1000 lines max — readers scan, they don't read.
- Mermaid diagrams over images — version-controllable as text, easier to update.
