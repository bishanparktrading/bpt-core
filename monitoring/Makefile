# Render grafanalib dashboards (Python) → Grafana JSON → generated/.
#
# Usage:
#   make venv     — create .venv and install grafanalib
#   make          — render every dashboards/*.py into generated/*.json
#   make clean    — wipe generated/
#   make up       — start the monitoring stack
#   make down     — stop it
#   make reload   — re-render + hot-reload Grafana provisioning

VENV      := .venv
PYTHON    := $(VENV)/bin/python
GEN       := $(VENV)/bin/generate-dashboard
SOURCES   := $(wildcard dashboards/*.dashboard.py)
TARGETS   := $(SOURCES:dashboards/%.dashboard.py=generated/%.json)

.PHONY: all venv clean up down reload

all: $(TARGETS)

venv: $(VENV)/bin/activate

$(VENV)/bin/activate: requirements.txt
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --quiet --upgrade pip
	$(VENV)/bin/pip install --quiet -r requirements.txt
	touch $@

generated/%.json: dashboards/%.dashboard.py | $(VENV)/bin/activate
	@mkdir -p generated
	$(GEN) --output $@ $<

clean:
	rm -rf generated/*.json

up: all
	docker compose up -d

down:
	docker compose down

reload: all
	# Provisioning rescans every 30s by default; this just nudges it.
	docker compose kill -s HUP grafana 2>/dev/null || true
