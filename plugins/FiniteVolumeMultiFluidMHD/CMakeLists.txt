LIST ( APPEND FiniteVolumeMultiFluidMHD_files
AUSMFluxMultiFluid.ci
AUSMFluxMultiFluid.cxx
AUSMFluxMultiFluid.hh
AUSMPlusUpFluxMultiFluid.ci
AUSMPlusUpFluxMultiFluid.hh
FiniteVolumeMultiFluidMHD.hh
HartmannSourceTerm.ci
HartmannSourceTerm.cxx
HartmannSourceTerm.hh
MirrorWall.cxx
MirrorWall.hh
MirrorWall3D.cxx
MirrorWall3D.hh
MultiFluidMHDST.ci
MultiFluidMHDST.cxx
MultiFluidMHDST.hh
MultiFluidMHDST3D.ci
MultiFluidMHDST3D.cxx
MultiFluidMHDST3D.hh
NoSlipWallIsothermalEIWRhoiViTi.cxx
NoSlipWallIsothermalEIWRhoiViTi.hh
NoSlipWallIsothermalPCWRhoiViTi.cxx
NoSlipWallIsothermalPCWRhoiViTi.hh
NSFluxMultiFluid.cxx
PerfectConductingWall.cxx
PerfectConductingWall.hh
PerfectConductingWall3D.cxx
PerfectConductingWall3D.hh
BCPeriodicMFMHD.cxx
BCPeriodicMFMHD.hh
SubInletUVTEIWRhoiViTi.cxx
SubInletUVTEIWRhoiViTi.hh
SubInletUVTPCWRhoiViTi.cxx
SubInletUVTPCWRhoiViTi.hh
SubOutletPEIWRhoiViTi.cxx
SubOutletPEIWRhoiViTi.hh
SubOutletPPCWRhoiViTi.cxx
SubOutletPPCWRhoiViTi.hh
SubOutletPPCWRhoiViTi3D.cxx
SubOutletPPCWRhoiViTi3D.hh
)

LIST ( APPEND FiniteVolumeMultiFluidMHD_requires_mods NavierStokes Maxwell MultiFluidMHD FiniteVolume FiniteVolumeNavierStokes )
LIST ( APPEND FiniteVolumeMultiFluidMHD_cflibs NavierStokes Maxwell MultiFluidMHD FiniteVolume FiniteVolumeNavierStokes )

CF_ADD_PLUGIN_LIBRARY ( FiniteVolumeMultiFluidMHD )

CF_WARN_ORPHAN_FILES()
