#!/bin/bash
(
  cd tests || exit
  ./build_tests.py
  chmod +x ./run_tests.py
  ./run_tests.py
)