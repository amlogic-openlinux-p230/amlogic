#!/bin/bash

source system/tools/hidl/update-makefiles-helper.sh

do_makefiles_update \
  "vendor.amlogic.hardware:vendor/amlogic/interfaces" \
  "android.hidl:system/libhidl/transport" \
  "android.hardware:hardware/interfaces"
  
#options="-r vendor.amlogic.hardware:vendor/amlogic/interfaces \
#         -r android.hidl:system/libhidl/transport \
#         -r android.hardware:hardware/interfaces"

#hidl-gen -Lmakefile $options vendor.amlogic.hardware.systemcontrol@1.0;
#hidl-gen -Landroidbp $options vendor.amlogic.hardware.systemcontrol@1.0;

