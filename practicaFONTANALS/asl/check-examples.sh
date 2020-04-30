#!/bin/bash

echo "BEGIN examples-initial/typecheck"
for f in ../examples/jpbasic_chkt_*.asl; do
    echo $(basename "$f")
    ./asl "$f" | egrep ^L > tmp.err; _asl=$?
    diff tmp.err "${f/asl/err}"; _diff=$?
    rm -f tmp.err
    if [[ $_asl != 0 || $_diff != 0 ]]; then exit; fi
done
echo "END   examples-initial/typecheck"

echo ""
echo "BEGIN examples-full/typecheck"
for f in ../examples/jp_chkt_*.asl; do
    echo $(basename $f)
    ./asl $f | egrep ^L > tmp.err; _asl=$?
    diff tmp.err ${f/asl/err}; _diff=$?
    rm -f tmp.err
    if [[ $_asl != 0 || $_diff != 0 ]]; then exit; fi
done
echo "END   examples-full/typecheck"

echo ""
echo "BEGIN examples-initial/codegen"
for f in ../examples/jpbasic_genc_*.asl; do
    echo $(basename $f)
    ./asl $f | egrep -v '^\(' > tmp.t; _asl=$?
    diff tmp.t ${f/asl/t}; _diff=$?
    rm -f tmp.t
    if [[ $_asl != 0 || $_diff != 0 ]]; then exit; fi
done
echo "END   examples-initial/codegen"

# echo ""
# echo "BEGIN examples-full/codegen"
# for f in ../examples/jp_genc_*.asl; do
#     echo $(basename "$f")
#     ./asl "$f" > tmp.t; _asl=$?
#     diff tmp.t "${f/asl/t}"; _diff=$?
#     rm -f tmp.t
#     if [[ $_asl != 0 || $_diff != 0 ]]; then exit; fi
# done
# echo "END   examples-full/codegen"

echo ""
echo "BEGIN examples-initial/execution"
for f in ../examples/jpbasic_genc_*.asl; do
    echo $(basename "$f")
    ./asl "$f" > tmp.t; _asl=$?
    ../tvm/tvm tmp.t < "${f/asl/in}" > tmp.out
    diff tmp.out "${f/asl/out}"; _diff=$?
    rm -f tmp.t tmp.out
    if [[ $_asl != 0 || $_diff != 0 ]]; then exit; fi
done
echo "END   examples-initial/execution"

# echo ""
# echo "BEGIN examples-full/execution"
# for f in ../examples/jp_genc_*.asl; do
#     echo $(basename "$f")
#     ./asl "$f" > tmp.t; _asl=$?
#     ../tvm/tvm tmp.t < "${f/asl/in}" > tmp.out
#     diff tmp.out "${f/asl/out}"; _diff=$?
#     rm -f tmp.t tmp.out
#     if [[ $_asl != 0 || $_diff != 0 ]]; then exit; fi
# done
# echo "END   examples-full/execution"
