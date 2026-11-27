#!/bin/sh
# spread.sh CLIP INDEX: one whole fit from the clip's start, nudged by float32's resolution (index 0:
# not nudged), as one line. How far the outcome moves is how path-dependent the fit is.
out=$(LEARNER_FIT=1 LEARNER_PERTURB=$2 ${TEST:-./test_learner} ${REF:-.}/$1 2>&1 | grep "300 steps in")
echo "$1 $2 $out"
