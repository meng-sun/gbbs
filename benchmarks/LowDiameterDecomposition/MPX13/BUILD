cc_library(
  name = "LowDiameterDecomposition",
  hdrs = ["LowDiameterDecomposition.h"],
  deps = [
  "//pbbslib:random_shuffle",
  "//ligra:ligra"
  ]
)

cc_binary(
  name = "LowDiameterDecomposition_main",
  srcs = ["LowDiameterDecomposition.cc"],
  deps = [":LowDiameterDecomposition"]
)

package(
  default_visibility = ["//visibility:public"],
)
