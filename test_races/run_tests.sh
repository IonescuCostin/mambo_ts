#!/bin/bash

for filepath in src/*.c; do
    filename=${filepath##*/}
    gcc "$filepath" -lpthread -fPIE -pie -g -o ./output/${filename::-1}out
done

rm results.txt
touch results.txt

for filepath in output/*.out; do
    filename=${filepath##*/}
    printf "%.80s\n\n" "======================== ${filename::-4} ================================================" >> results.txt

    ./"$filepath" |& grep "WARNING:" >> results.txt

    printf "\n" >> results.txt
done