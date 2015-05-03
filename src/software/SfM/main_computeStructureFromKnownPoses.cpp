
// Copyright (c) 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "openMVG/sfm/sfm.hpp"
#include "nonFree/sift/SIFT_describer.hpp"
#include <cereal/archives/json.hpp>
#include "openMVG/system/timer.hpp"

#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"

using namespace openMVG;

/// Build a list of pair from the camera frusta intersections
Pair_Set BuildPairsFromFrustumsIntersections(
  const SfM_Data & sfm_data,
  const double z_near = -1., // default near plane
  const double z_far = -1.)  // default far plane
{
  const Frustum_Filter frustum_filter(sfm_data, z_near, z_far);
  return frustum_filter.getFrustumIntersectionPairs();
}

/// Compute the structure of a scene according existing camera poses.
int main(int argc, char **argv)
{
  using namespace std;
  std::cout << "Compute Structure from the provided poses" << std::endl;

  CmdLine cmd;

  std::string sSfM_Data_Filename;
  std::string sMatchesDir;
  std::string sMatchFile;
  std::string sOutFile = "";

  cmd.add( make_option('i', sSfM_Data_Filename, "input_file") );
  cmd.add( make_option('m', sMatchesDir, "match_dir") );
  cmd.add( make_option('f', sMatchFile, "match_file") );
  cmd.add( make_option('o', sOutFile, "output_file") );

  try {
    if (argc == 1) throw std::string("Invalid command line parameter.");
    cmd.process(argc, argv);
  } catch(const std::string& s) {
    std::cerr << "Usage: " << argv[0] << '\n'
    << "[-i|--input_file] path to a SfM_Data scene\n"
    << "[-m|--match_dir] path to the features and descriptor that "
    << " corresponds to the provided SfM_Data scene\n"
    << "[-f|--match_file] (opt.) path to a matches file (used pairs will be used)\n"
    << "[-o|--output_file] file where the output data will be stored\n"
    << std::endl;

    std::cerr << s << std::endl;
    return EXIT_FAILURE;
  }

  // Load input SfM_Data scene
  SfM_Data sfm_data;
  if (!Load(sfm_data, sSfM_Data_Filename, ESfM_Data(VIEWS|INTRINSICS|EXTRINSICS))) {
    std::cerr << std::endl
      << "The input SfM_Data file \""<< sSfM_Data_Filename << "\" cannot be read." << std::endl;
    return EXIT_FAILURE;
  }

  // Init the image describer (used for regions loading)
  using namespace openMVG::features;
  std::unique_ptr<Image_describer> image_describer;
  const std::string sImage_describer = stlplus::create_filespec(sMatchesDir, "image_describer", "json");
  if (stlplus::is_file(sImage_describer))
  {
    // Dynamically load the image_describer from the file (will restore old used settings)
    std::ifstream stream(sImage_describer.c_str());
    if (!stream.is_open())
      return false;

    cereal::JSONInputArchive archive(stream);
    archive(cereal::make_nvp("image_describer", image_describer));
  }
  else // By default init a SIFT_Image_describer (keep compatibility)
  {
    image_describer.reset(new SIFT_Image_describer());
  }
  // Prepare the Regions provider
  std::shared_ptr<Regions_Provider> regions_provider = std::make_shared<Regions_Provider>();
  if (!regions_provider->load(sfm_data, sMatchesDir, image_describer)) {
    std::cerr << std::endl
      << "Invalid regions." << std::endl;
    return EXIT_FAILURE;
  }

  //--
  //- Pair selection method
  //  - geometry guided -> camera frustum intersection
  //  - putative matches guided (photometric matches) (Keep pair that have valid Intrinsic & Pose ids)
  //--
  Pair_Set pairs;
  if (sMatchFile.empty())
  {
    // no provided pair, use camera frustum intersection
    pairs = BuildPairsFromFrustumsIntersections(sfm_data);
  }
  else
  {
    PairWiseMatches matches;
    if (!matching::PairedIndMatchImport(sMatchFile, matches)) {
      std::cerr<< "Unable to read the matches file." << std::endl;
      return EXIT_FAILURE;
    }
    pairs = getPairs(matches);
  }

  // Keep only Pairs that belong to valid view indexes.
  std::set<IndexT> valid_viewIdx = Get_Valid_Views(sfm_data);
  pairs = Pair_filter(pairs, valid_viewIdx);

  openMVG::Timer timer;

  //------------------------------------------
  // Compute Structure from known camera poses
  //------------------------------------------
  SfM_Data_Structure_Estimation_From_Known_Poses structure_estimator;
  structure_estimator.run(sfm_data, pairs, regions_provider);
  RemoveOutliers_AngleError(sfm_data, 2.0);

  std::cout << "\nStructure estimation took (s): " << timer.elapsed() << "." << std::endl;

  std::cout << "#landmark found: " << sfm_data.getLandmarks().size() << std::endl;

  if (stlplus::extension_part(sOutFile) != "ply") {
    Save(sfm_data,
      stlplus::create_filespec(
        stlplus::folder_part(sOutFile),
        stlplus::basename_part(sOutFile), "ply"),
      ESfM_Data(ALL));
  }

  if (Save(sfm_data, sOutFile, ESfM_Data(ALL)))
    return EXIT_SUCCESS;
  else
    return EXIT_FAILURE;
}
