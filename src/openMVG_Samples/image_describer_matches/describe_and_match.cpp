
// Copyright (c) 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "openMVG/image/image.hpp"
#include "openMVG/features/features.hpp"
#include "openMVG/matching/matcher_brute_force.hpp"
#include "openMVG/matching/matcher_kdtree_flann.hpp"
#include "openMVG/matching/matching_filters.hpp"
#include "openMVG_Samples/siftPutativeMatches/two_view_matches.hpp"

#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include "third_party/vectorGraphics/svgDrawer.hpp"
#include "third_party/cmdLine/cmdLine.h"

#include "nonFree/sift/SIFT_describer.hpp"

#include <string>
#include <iostream>

using namespace openMVG;
using namespace svg;
using namespace std;

int main(int argc, char **argv) {

  // Add options to choose the desired Image_describer
  std::string sImage_describer_type = "SIFT";

  CmdLine cmd;
  cmd.add( make_option('t', sImage_describer_type, "type") );

  try {
      if (argc == 1) throw std::string("Invalid command line parameter.");
      cmd.process(argc, argv);
  } catch(const std::string& s) {
      std::cerr << "Usage: " << argv[0] << '\n'
      << "\n[Optional]\n"
      << "[-t|--type\n"
      << "  (choose an image_describer interface):\n"
      << "   SIFT: SIFT keypoint & descriptor,\n"
      << "   AKAZE: AKAZE keypoint & floating point descriptor]"
      << std::endl;

      std::cerr << s << std::endl;
      return EXIT_FAILURE;
  }

  Image<RGBColor> image;
  const string jpg_filenameL = stlplus::folder_up(string(THIS_SOURCE_DIR))
    + "/imageData/StanfordMobileVisualSearch/Ace_0.png";
  const string jpg_filenameR = stlplus::folder_up(string(THIS_SOURCE_DIR))
    + "/imageData/StanfordMobileVisualSearch/Ace_1.png";

  Image<unsigned char> imageL, imageR;
  ReadImage(jpg_filenameL.c_str(), &imageL);
  ReadImage(jpg_filenameR.c_str(), &imageR);

  // Call Keypoint extractor
  using namespace openMVG::features;
  std::shared_ptr<Image_describer> image_describer;
  if (sImage_describer_type == "SIFT")
    image_describer = std::make_shared<SIFT_Image_describer>(SiftParams());
  else if (sImage_describer_type == "AKAZE")
    image_describer = std::make_shared<AKAZE_Image_describer>(AKAZEParams(AKAZEConfig(), AKAZE_MSURF));
  else if (sImage_describer_type == "AKAZE_MLDB")
    image_describer = std::make_shared<AKAZE_Image_describer>(AKAZEParams(AKAZEConfig(), AKAZE_MLDB));

  if (image_describer.use_count()==0)
  {
    std::cerr << "Invalid Image_describer type" << std::endl;
    return EXIT_FAILURE;
  }

  //--
  // Detect regions thanks to the image_describer
  //--
  std::map<IndexT, std::unique_ptr<features::Regions> > regions_perImage;
  image_describer->Describe(imageL, regions_perImage[0]);
  image_describer->Describe(imageR, regions_perImage[1]);

  const std::vector<PointFeature>
    featsL = regions_perImage.at(0)->GetRegionsPositions(),
    featsR = regions_perImage.at(1)->GetRegionsPositions();

  //--
  // Display used images & Features
  //--

  {
    //- Show images side by side
    Image<unsigned char> concat;
    ConcatH(imageL, imageR, concat);
    const string out_filename = "00_images.jpg";
    WriteImage(out_filename.c_str(), concat);
  }

  {
    //- Draw features on the images (side by side)
    Image<unsigned char> concat;
    ConcatH(imageL, imageR, concat);

    //-- Draw features :
    for (size_t i=0; i < featsL.size(); ++i )  {
      const PointFeature & imaA = featsL[i];
      DrawCircle(imaA.x(), imaA.y(), 3.0f, 255, &concat);
    }
    for (size_t i=0; i < featsR.size(); ++i )  {
      const PointFeature & imaB = featsR[i];
      DrawCircle(imaB.x()+imageL.Width(), imaB.y(), 3.0f, 255, &concat);
    }
    const string out_filename = "01_features.jpg";
    WriteImage(out_filename.c_str(), concat);
  }

  //--
  // Compute corresponding points
  //--
  using namespace openMVG::matching;
  //-- Perform matching -> find Nearest neighbor, filtered with Distance ratio
  std::vector<IndMatch> vec_PutativeMatches;
  {
    // Use dynamic cast on Regions type to use the appropriate matcher
    if (regions_perImage.at(0)->IsScalar() && regions_perImage.at(1)->IsScalar())
    {
      if( dynamic_cast<SIFT_Regions*>(regions_perImage.at(0).get()) != NULL )
      {
        typedef SIFT_Regions::DescriptorT DescriptorT;
        typedef L2_Vectorized<DescriptorT::bin_type> Metric;
        typedef ArrayMatcherBruteForce<DescriptorT::bin_type, Metric> MatcherT;
        // Distance ratio squared due to squared metric
        getPutativesMatches<DescriptorT, MatcherT>(
          ((SIFT_Regions*)regions_perImage.at(0).get())->Descriptors(),
          ((SIFT_Regions*)regions_perImage.at(1).get())->Descriptors(),
          Square(0.8), vec_PutativeMatches);
      }

      if( dynamic_cast<AKAZE_Float_Regions*>(regions_perImage.at(0).get()) != NULL )
      {
        typedef AKAZE_Float_Regions::DescriptorT DescriptorT;
        typedef L2_Vectorized<DescriptorT::bin_type> Metric;
        typedef ArrayMatcherBruteForce<DescriptorT::bin_type, Metric> MatcherT;
        // Distance ratio squared due to squared metric
        getPutativesMatches<DescriptorT, MatcherT>(
          ((AKAZE_Float_Regions*)regions_perImage.at(0).get())->Descriptors(),
          ((AKAZE_Float_Regions*)regions_perImage.at(1).get())->Descriptors(),
          Square(0.8), vec_PutativeMatches);
      }
    }
    else  // Binary regions
    {
      if( dynamic_cast<AKAZE_Binary_Regions*>(regions_perImage.at(0).get()) != NULL )
      {
        typedef AKAZE_Binary_Regions::DescriptorT DescriptorT;
        typedef Hamming<unsigned char> Metric;
        typedef ArrayMatcherBruteForce<unsigned char, Metric> MatcherT;
        getPutativesMatches<DescriptorT, MatcherT>(
          ((AKAZE_Binary_Regions*)regions_perImage.at(0).get())->Descriptors(),
          ((AKAZE_Binary_Regions*)regions_perImage.at(1).get())->Descriptors(),
          0.8, vec_PutativeMatches);
      }
    }
  }

  // Draw correspondences after Nearest Neighbor ratio filter
  {
    svgDrawer svgStream( imageL.Width() + imageR.Width(), max(imageL.Height(), imageR.Height()));
    svgStream.drawImage(jpg_filenameL, imageL.Width(), imageL.Height());
    svgStream.drawImage(jpg_filenameR, imageR.Width(), imageR.Height(), imageL.Width());
    for (size_t i = 0; i < vec_PutativeMatches.size(); ++i) {
      //Get back linked feature, draw a circle and link them by a line
      const PointFeature & L = featsL[vec_PutativeMatches[i]._i];
      const PointFeature & R = featsR[vec_PutativeMatches[i]._j];
      svgStream.drawLine(L.x(), L.y(), R.x()+imageL.Width(), R.y(), svgStyle().stroke("green", 2.0));
      svgStream.drawCircle(L.x(), L.y(), 3.0f, svgStyle().stroke("yellow", 2.0));
      svgStream.drawCircle(R.x()+imageL.Width(), R.y(), 3.0f,svgStyle().stroke("yellow", 2.0));
    }
    const string out_filename = "02_Matches.svg";
    ofstream svgFile( out_filename.c_str() );
    svgFile << svgStream.closeSvgFile().str();
    svgFile.close();
  }

  // Display some statistics
  std::cout
    << regions_perImage.at(0)->RegionCount() << " #Features on image A" << std::endl
    << regions_perImage.at(1)->RegionCount() << " #Features on image B" << std::endl
    << vec_PutativeMatches.size() << " #matches with Distance Ratio filter" << std::endl;

  return EXIT_SUCCESS;
}
