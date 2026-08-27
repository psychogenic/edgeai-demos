#!/bin/bash


MAINDIR=$1
RANDOM_PRE=${2:-0}
TARGETS="a c d e h i l n o r s t SPACE DOT BACK ENTER"


FIXED_LEN_SAMPLES=2048
#FIXED_PRE_EXPAND=300
FIXED_PRE_EXPAND=400

DEFAULT_RANDOM_PRE=0
ADD_RANDOM_SHIFT=0
DO_NORMALIZED_CLEANUP=0
SPLIT_VALID_SET=0

if [ "x$MAINDIR" == "x" ]
then
	echo "USAGE: $0 OUTPUTDIR [RANDOM_PRE]"
	exit 1
fi


# source sessions.sh
SESSIONS=`seq -s " " 1 20 `


split_files_into_train_val() {
  # Usage: split_files_into_train_val "pattern" [train_percent]
  # Example: split_files_into_train_val "${target}*_${SESSNAME}_*.bin"
  #          split_files_into_train_val "${target}*_${SESSNAME}_*.bin" 80
  #
  # Sets two global arrays:  training[]  and  validation[]

  local pattern="$1"
  local train_percent="${2:-80}"   # default 80%

  # Safety checks
  if [[ -z "$pattern" ]]; then
    echo "Usage: split_files_into_train_val \"pattern\" [train_percent]" >&2
    return 1
  fi
  if ! [[ "$train_percent" =~ ^[0-9]+$ ]] || (( train_percent < 1 || train_percent > 99 )); then
    echo "train_percent must be an integer between 1 and 99" >&2
    return 1
  fi

  # Collect matching files
  shopt -s nullglob
  local files=( $pattern )
  shopt -u nullglob

  if (( ${#files[@]} == 0 )); then
    echo "No matching files found for pattern: $pattern" >&2
    return 1
  fi

  # Shuffle randomly
  mapfile -t shuffled < <(printf '%s\n' "${files[@]}" | shuf)

  local total=${#shuffled[@]}
  local train_count=$(( total * train_percent / 100 ))

  # Populate the two global arrays
  training=( "${shuffled[@]:0:train_count}" )
  validation=( "${shuffled[@]:train_count}" )

  echo "Total files : $total"
  echo "Training    : ${#training[@]}  (${train_percent}%)"
  echo "Validation  : ${#validation[@]}  ($((100 - train_percent))%)"
}


for target in $TARGETS
do 
  echo $target
  DESTDIR=$MAINDIR/$target
  if [ -e $DESTDIR ]
  then
     echo "Storing in $DESTDIR"
  else
     mkdir $DESTDIR
  fi
  for sess in $SESSIONS
  do 
     SESSNAME="s$sess"
     SESSDIR="sessions/$SESSNAME"
     ./normalizesamples.py  --fixed-length $FIXED_LEN_SAMPLES \
           -o $DESTDIR --pre-expand $FIXED_PRE_EXPAND --random-pre $RANDOM_PRE $SESSDIR/$target 
           # -o $DESTDIR  --pre-expand 300 --random-pre $RANDOM_PRE $SESSDIR/$target 
     TRAIN_NAME="train_${target}_${SESSNAME}"
     VALIDATION_NAME="valid_${target}_${SESSNAME}"
     
     if [ "$SPLIT_VALID_SET" -gt "0" ]
     then
          split_files_into_train_val "$DESTDIR/${target}*_${SESSNAME}_*.bin" 80
          
     else
          training=("$DESTDIR/${target}*_${SESSNAME}_*.bin")
     
     fi
     
     if [ "$ADD_RANDOM_SHIFT" -gt "0" ]
     then
     
          ./normalizesamples.py  --fixed-length $FIXED_LEN_SAMPLES \
                -o $DESTDIR/r1  --pre-expand 400 --random-pre 500 $SESSDIR/$target
                
          #./normalizesamples.py  --fixed-length $FIXED_LEN_SAMPLES \
          #     -o $DESTDIR/r2  --pre-expand 250 --random-pre 600 $SESSDIR/$target
     
          train_r1=( "${training[@]//\/$target\//\/$target\/r1\/}" )
          #train_r2=( "${training[@]//\/$target\//\/$target\/r2\/}" )
          #cat ${train_r1[@]} ${training[@]} ${train_r2[@]} > $DESTDIR/${TRAIN_NAME}.bin
          cat ${train_r1[@]} ${training[@]} > $DESTDIR/${TRAIN_NAME}.bin
           
          if [ "$SPLIT_VALID_SET" -gt "0"  ]
          then
               valid_r1=( "${validation[@]//\/$target\//\/$target\/r1\/}" )
               #valid_r2=( "${validation[@]//\/$target\//\/$target\/r2\/}" )
               
               cat ${valid_r1[@]} ${validation[@]} > $DESTDIR/${VALIDATION_NAME}.bin
               #cat ${valid_r1[@]} ${validation[@]} ${valid_r2[@]} > $DESTDIR/${VALIDATION_NAME}.bin
               
          fi
     else
          cat ${training[@]} > $DESTDIR/${TRAIN_NAME}.bin
     fi
   
     
     
  done
  # done loop over sessions
  
  cat $DESTDIR/train_*.bin >> $MAINDIR/all_train_${target}.bin
  ./rescale.py $MAINDIR/all_train_${target}.bin $MAINDIR/all_train_${target}-scaled.bin 
  if [ "$SPLIT_VALID_SET" -gt "0"  ]
  then     

     cat $DESTDIR/valid_*.bin > $MAINDIR/all_valid_${target}.bin
     ./rescale.py $MAINDIR/all_valid_${target}.bin $MAINDIR/all_valid_${target}-scaled.bin 
  fi
     
  if [ "$DO_NORMALIZED_CLEANUP" -gt "0" ]
  then
          rm $DESTDIR/${target}_*.bin 
          if [ "$ADD_RANDOM_SHIFT" -gt "0" ]
          then
               rm $DESTDIR/r1/${target}_*.bin
               rm $DESTDIR/r2/${target}_*.bin
          fi
  fi
done
# done loop over all targets

echo "Copying over idle samples"
#echo "Copying over random and idle samples"
cp sessions/idle/* $MAINDIR
# cp sessions/random/* $MAINDIR


for target in $TARGETS
do 
     NUMSAMPS=$(ls -1 $MAINDIR/${target}*/${target}*_s*.bin | wc -l)
     echo -e "$target:\t$NUMSAMPS samples"
done

echo Done
