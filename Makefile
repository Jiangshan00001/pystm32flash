PYTHON ?= python3

.PHONY: dist doc dep_install
dep_install:
	$(PYTHON) -m pip install setuptools wheel twine

dist:
	$(PYTHON) setup.py mybuild --installprefix=./pystm32flash
	$(PYTHON) setup.py sdist bdist_wheel

.PHONY: upload
upload:
	$(PYTHON) -m twine upload dist/*
clean:
	rm -rf build dist pystm32flash.egg-info *.bak build_setup_py
doc:
	pydoc-markdown -I pyuwb -m pyuwb -m uwb_modbus --render-toc > doc/pyuwb_api.md

.PHONY: test
test:
	pytest

