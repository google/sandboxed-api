#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iostream>

#include "gdal_sapi.sapi.h"
#include "sandboxed_api/sandbox2/util/fileops.h"

class GdalSapiSandbox : public gdalSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .DangerDefaultAllowAll()
        .DisableNamespaces()
        .BuildOrDie();
  }
};

int main() {
  GdalSapiSandbox sandbox;
  sandbox.Init().IgnoreError();
  gdalApi api(&sandbox);

  // Reading GDALDataset from a (local, specific) file.
  std::string filename = "CANYrelief1-geo.tif";
  sapi::v::CStr s(filename.data());

  api.GDALAllRegister().IgnoreError();
  auto open = api.GDALOpen(s.PtrBefore(), GDALAccess::GA_ReadOnly);
  sapi::v::RemotePtr ptrDataset(open.value());

  LOG(INFO) << "Dataset pointer adress: " << open.value() << std::endl;
  LOG(INFO) << ptrDataset.ToString() << std::endl;
  if (!open.value()) {
    printf("NULL pointer for Dataset.\n");
    return 1;
  }

  // Printing some general information about the dataset.
  auto driver = api.GDALGetDatasetDriver(&ptrDataset);
  sapi::v::RemotePtr ptrDriver(driver.value());

  auto driverShortName = api.GDALGetDriverShortName(&ptrDriver);
  auto driverLongName = api.GDALGetDriverLongName(&ptrDriver);

  sapi::v::RemotePtr ptrDriverShortName(driverShortName.value());
  sapi::v::RemotePtr ptrDriverLongName(driverLongName.value());

  LOG(INFO) << "Driver short name: "
            << sandbox.GetCString(ptrDriverShortName).value().c_str();
  LOG(INFO) << "Driver long name: "
            << sandbox.GetCString(ptrDriverLongName).value().c_str();

  // Checking that GetGeoTransform is valid.
  std::vector<double> adfGeoTransform(6);
  sapi::v::Array<double> adfGeoTransformArray(&adfGeoTransform[0],
                                              adfGeoTransform.size());

  api.GDALGetGeoTransform(&ptrDataset, adfGeoTransformArray.PtrBoth())
      .IgnoreError();

  LOG(INFO) << "Origin = (" << adfGeoTransform[0] << ", " << adfGeoTransform[3]
            << ")" << std::endl;
  LOG(INFO) << "Pixel Size = (" << adfGeoTransform[0] << ", "
            << adfGeoTransform[3] << ")" << std::endl;

  std::vector<int> nBlockXSize(1);
  std::vector<int> nBlockYSize(1);

  sapi::v::Array<int> nBlockXSizeArray(&nBlockXSize[0], nBlockXSize.size());
  sapi::v::Array<int> nBlockYSizeArray(&nBlockYSize[0], nBlockYSize.size());

  auto band = api.GDALGetRasterBand(&ptrDataset, 1);
  LOG(INFO) << "Band pointer adress: " << band.value() << std::endl;
  if (!band.value()) {
    printf("NULL pointer for Band.\n");
    return 1;
  }

  sapi::v::RemotePtr ptrBand(band.value());
  api.GDALGetBlockSize(&ptrBand, nBlockXSizeArray.PtrBoth(),
                       nBlockYSizeArray.PtrBoth())
      .IgnoreError();

  LOG(INFO) << "Block = " << nBlockXSize[0] << " x " << nBlockYSize[0]
            << std::endl;

  std::vector<int> bGotMin(1);
  std::vector<int> bGotMax(1);

  sapi::v::Array<int> bGotMinArray(&bGotMin[0], bGotMin.size());
  sapi::v::Array<int> bGotMaxArray(&bGotMax[0], bGotMax.size());

  auto adfMin = api.GDALGetRasterMinimum(&ptrBand, bGotMinArray.PtrBoth());
  auto adfMax = api.GDALGetRasterMaximum(&ptrBand, bGotMaxArray.PtrBoth());

  auto nXSize = api.GDALGetRasterBandXSize(&ptrBand);
  auto nYSize = api.GDALGetRasterBandYSize(&ptrBand);

  std::vector<int8_t> rasterData(nXSize.value() * nYSize.value(), -1);
  sapi::v::Array<int8_t> rasterDataArray(&rasterData[0], rasterData.size());

  api.GDALRasterIO(&ptrBand, GF_Read, 0, 0, nXSize.value(), nYSize.value(),
                   rasterDataArray.PtrBoth(), nXSize.value(), nYSize.value(),
                   GDT_Byte, 0, 0)
      .IgnoreError();

  std::cout << "Raster data info: " << rasterDataArray.ToString() << std::endl;

  // To print the data content: `std::cout << rasterDataArray.GetData() <<
  // std::endl;`

  return 0;
}