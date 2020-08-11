package(default_visibility = ["//visibility:public"])

cc_library(
    name = "guetzli_lib",
    srcs = glob(
        [
            "guetzli/*.h",
            "guetzli/*.cc",
            "guetzli/*.inc",
        ],
        exclude = ["guetzli/guetzli.cc"],
    ),
    copts = [ "-Wno-sign-compare" ],
    visibility= [ "//visibility:public" ],
    deps = [
        "@butteraugli//:butteraugli_lib",
    ],
)