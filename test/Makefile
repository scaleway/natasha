
VENV_NAME?=.venv
VENV_ACTIVATE=. $(VENV_NAME)/bin/activate
PYTHON=${VENV_NAME}/bin/python

.PHONY: help
.DEFAULT: help
help:
	@echo "make venv"
	@echo "		prepare the test environement with dependencies"
	@echo "make clean"
	@echo "		clean the test environement and pyc files"

.PHONY: venv
venv: $(VENV_NAME)/bin/activate
$(VENV_NAME)/bin/activate: requirements.txt
	test -d $(VENV_NAME) || virtualenv -p python $(VENV_NAME)
	${PYTHON} -m pip install -Ur requirements.txt
	touch $(VENV_NAME)/bin/activate
	ln -rsf ntfc $(VENV_NAME)/bin/ntfc
	ln -rsf gen_conf.py $(VENV_NAME)/bin/gen_conf
	make activate

.PHONY: activate
activate:
	. $(VENV_NAME)/bin/activate

.PHONY: clean
clean:
	find . -name "*.pyc" -exec rm -f {} \;
	rm -rf $(VENV_NAME)
