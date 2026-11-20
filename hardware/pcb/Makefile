# Silentype-Pico carrier: place -> autoroute -> pours/silk -> DRC -> fab outputs.
#   make            full pipeline (needs KiCad 10, Java 25, Freerouting 2.5 jar)
#   make FREEROUTING_JAR=/path/to/freerouting-2.5.0.jar
JAVA            ?= /usr/lib/jvm/java-25-openjdk/bin/java
FREEROUTING_JAR ?= $(HOME)/.local/share/freerouting/freerouting-2.5.0.jar
PCB  := silentype-carrier.kicad_pcb
OUT  := fab

.PHONY: all place route finish drc fab render models clean
all: drc fab render

place:
	python3 gen_resistor_models.py
	python3 build_carrier.py place
route: place
	$(JAVA) -Djava.awt.headless=true -jar $(FREEROUTING_JAR) --gui.enabled=false \
	  -de build/silentype-carrier.dsn -do build/silentype-carrier.ses -mp 200 > build/freerouting.log 2>&1
	@grep -E "Auto-routing stage completed" build/freerouting.log | sed 's/.*final score/route score/'
finish: route
	python3 build_carrier.py finish
drc: finish
	kicad-cli pcb drc --severity-all --format json -o build/drc.json $(PCB) > /dev/null
	kicad-cli pcb drc --severity-all -o $(OUT)/drc-report.txt $(PCB) > /dev/null || true
	python3 check_drc.py build/drc.json
fab:
	mkdir -p $(OUT)/gerbers
	kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
	  --subtract-soldermask -o $(OUT)/gerbers/ $(PCB)
	kicad-cli pcb export drill --format excellon --excellon-separate-th --generate-map --map-format pdf \
	  -o $(OUT)/gerbers/ $(PCB)
	cd $(OUT)/gerbers && rm -f ../silentype-carrier-gerbers.zip && zip -q ../silentype-carrier-gerbers.zip *
	kicad-cli pcb export pos --format csv --units mm -o $(OUT)/silentype-carrier-pos.csv $(PCB)
# 3D models: system kicad-library-3d if installed, else the subset fetched by `make models`.
MODELS_DIR ?= $(if $(wildcard /usr/share/kicad/3dmodels/Package_DIP.3dshapes),/usr/share/kicad/3dmodels,$(HOME)/.local/share/kicad/3dmodels)
RENDER := KICAD10_3DMODEL_DIR=$(MODELS_DIR) kicad-cli pcb render --quality high --background opaque
render:
	mkdir -p $(OUT)/render
	$(RENDER) --side top    --width 2400 --height 1500 -o $(OUT)/render/top.png $(PCB)
	$(RENDER) --side bottom --width 2400 --height 1500 -o $(OUT)/render/bottom.png $(PCB)
	$(RENDER) --side top --perspective --rotate "-55,0,25" --zoom 0.95 --floor --width 2400 --height 1600 -o $(OUT)/render/iso.png $(PCB)
	mkdir -p build/bare && python3 build_carrier.py bare
	$(RENDER) --side top    --width 2400 --height 1500 -o $(OUT)/render/bare-top.png build/bare/silentype-carrier.kicad_pcb
	$(RENDER) --side bottom --width 2400 --height 1500 -o $(OUT)/render/bare-bottom.png build/bare/silentype-carrier.kicad_pcb
# Fetch only the 3D models this board uses from KiCad's official library (no sudo).
models:
	python3 -c "import pcbnew; b=pcbnew.LoadBoard('$(PCB)'); print('\n'.join(sorted({m.m_Filename.split('}/')[-1] for f in b.GetFootprints() for m in f.Models()})))" | \
	while read f; do mkdir -p "$(MODELS_DIR)/$$(dirname $$f)"; [ -s "$(MODELS_DIR)/$$f" ] || \
	  curl -sfL -o "$(MODELS_DIR)/$$f" "https://gitlab.com/kicad/libraries/kicad-packages3D/-/raw/master/$$f" || echo "no model: $$f"; done
clean:
	rm -rf build $(OUT)
