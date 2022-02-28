build_example(
  NAME residential-example
  SOURCE_FILES residential-example.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libdocsis}
    ${libapplications}
    ${libtraffic-control}
    ${libbridge}
    ${libcsma}
    ${libinternet}
    ${libpoint-to-point}
)

build_example(
  NAME docsis-configuration-example
  SOURCE_FILES docsis-configuration-example.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libdocsis}
)

build_example(
  NAME simple-docsislink
  SOURCE_FILES simple-docsislink.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libconfig-store}
    ${libnetwork}
    ${libbridge}
    ${libdocsis}
    ${libpoint-to-point}
    ${libmobility}
    ${libinternet}
    ${libtraffic-control}
    ${libapplications}
    ${libflow-monitor}
)
