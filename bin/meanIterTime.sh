#!/bin/bash

files=""
if [[ $# == 1 && -d $1 ]]; then
    files=`ls $1/*.log`
else
    files=$@
fi

grep -B 1 "sample time" $files |
    awk '$0 ~ /resample_arg_regions/ {subtree=1};
         $0 ~ /resample_arg_leaf/ {subtree=0}
         $NF=="ms" {if (subtree==1) {
              totalSubtreeSec += $(NF-1)/1000; subtreeCount++;
            } else {
              totalLeafSec += $(NF-1)/1000; leafCount++;
         }}
         $NF=="s" {if (subtree==1) {
              totalSubtreeSec += $(NF-1); subtreeCount++
            } else {
              totalLeafSec += $(NF-1); leafCount++;
          }}
         $NF=="m" {if (subtree==1) {
              totalSubtreeSec += $(NF-1)*60; subtreeCount++;
            } else {
              totalLeafSec += $(NF-1)*60; leafCount++;}
         }
         $NF=="h" {if (subtree==1) {
              totalSubtreeSec += $(NF-1)*3600; subtreeCount++;
            } else {
              totalLeafSec += $(NF-1)*3600; leafCount++;}
         }
         END{avgLeaf=totalLeafSec/60/leafCount;
             avgSubtree=totalSubtreeSec/60/subtreeCount;
             print "leaf: "avgLeaf" m (out of "leafCount");  subtree: "avgSubtree" m; (out of "subtreeCount")"}'
