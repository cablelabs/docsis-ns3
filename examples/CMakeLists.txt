build_lib_example(
  NAME docsis-configuration-example
  SOURCE_FILES docsis-configuration-example.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libdocsis}
    ${libnetwork}
)

build_lib_example(
  NAME residential-example
  SOURCE_FILES residential-example.cc
  LIBRARIES_TO_LINK
    ${libapplications}
    ${libbridge}
    ${libcore}
    ${libcsma}
    ${libdocsis}
    ${libinternet}
    ${libnetwork}
    ${libpoint-to-point}
    ${libtraffic-control}
)

build_lib_example(
  NAME delay-estimation
  SOURCE_FILES delay-estimation.cc
  LIBRARIES_TO_LINK
    ${libapplications}
    ${libbridge}
    ${libcore}
    ${libdocsis}
    ${libinternet}
    ${libmobility}
    ${libnetwork}
    ${libpoint-to-point}
    ${libtraffic-control}
)

build_lib_example(
  NAME simple-docsislink
  SOURCE_FILES simple-docsislink.cc
  LIBRARIES_TO_LINK
    ${libapplications}
    ${libbridge}
    ${libconfig-store}
    ${libcore}
    ${libcsma}
    ${libdocsis}
    ${libflow-monitor}
    ${libinternet}
    ${libmobility}
    ${libnetwork}
    ${libpoint-to-point}
    ${libtraffic-control}
)
