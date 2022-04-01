#!/usr/bin/env bash

## mogrify all the files
find -name '*.exr' | xargs -n 16 -P 8 magick mogrify -format png -alpha off -monitor
# re-encode flip PNGs since their compression rates are abysmal
find -name 'flip*.png' | xargs -n 16 -P 8 magick mogrify -depth 24 -define png:compression-filter=5 -define png:compression-level=6 -define png:compression-strategy=1

## copy relevant files for comparison

# generate directory structure
mkdir -p upload/{alpha,bundle,filter,iters,limit,repro,statsteps}

# regular handling
for catg in alpha bundle iters repro statsteps; do
  for dir in $catg*; do
    # copy perf data files
    for i in $(seq 0 5); do
      test -f $dir/out_$i.txt && cp $dir/out_$i.txt upload/$catg/${dir#$catg}_$i.txt
    done
    # copy other files
    for name in t0 t1 t2_1 t3_1 t4_128 t5_9; do
      cp $dir/$name.png upload/$catg/${dir#$catg}_$name.png
      cp $dir/flip.$name.*.png upload/$catg/${dir#$catg}_$name.flip.png
      cat $dir/$name.flip.csv >>upload/$catg/all.flip.csv
    done
  done
  sed -i '3~2d' upload/$catg/all.flip.csv
done

# special for limit: only want t0 and t5_9
for dir in limit*; do
  # copy perf data files
  for i in $(seq 0 5); do
    test -f $dir/out_$i.txt && cp $dir/out_$i.txt upload/limit/${dir#limit}_$i.txt
  done
  # copy other files
  for name in t0 t5_9; do
    cp $dir/$name.png upload/limit/${dir#limit}_$name.png
    cp $dir/flip.$name.*.png upload/limit/${dir#limit}_$name.flip.png
    cat $dir/$name.flip.csv >>upload/limit/all.flip.csv
  done
done
sed -i '3~2d' upload/limit/all.flip.csv

# special for filters: directories have an underscore
for dir in filter*; do
  # copy perf data files
  for i in $(seq 0 5); do
    test -f $dir/out_$i.txt && cp $dir/out_$i.txt upload/filter/${dir#filter_}_$i.txt
  done
  # copy other files
  for name in t0 t5_9; do
    cp $dir/$name.png upload/filter/${dir#filter_}_$name.png
    cp $dir/flip.$name.*.png upload/filter/${dir#filter_}_$name.flip.png
    cat $dir/$name.flip.csv >>upload/filter/all.flip.csv
  done
done
sed -i '3~2d' upload/filter/all.flip.csv

# presets
for dir in quality fast balanced; do
  for i in $(seq 0 5); do
    cp $dir/out_$i.txt upload/${dir}_$i.txt
  done
  for name in t0 t1 t2_1 t3_1 t4_128 t5_9; do
    cp $dir/$name.png upload/${dir}_$name.png
    cp $dir/flip.$name.*.png upload/${dir}_$name.flip.png
    cat $dir/$name.flip.csv >>upload/all.flip.csv
  done
done
sed -i '3~2d' upload/all.flip.csv

# reference
for name in t0 t1 t2_1 t3_1 t4_128 t5_9; do
  cp reference/$name.png upload/ref_$name.png
done
