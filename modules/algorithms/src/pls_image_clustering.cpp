/*L*************************************************************************************************
*
*  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
*
*  By downloading, copying, installing or using the software you agree to this license. If you do
*  not agree to this license, do not download, install, copy or use the software.
*
*                            Software License Agreement (BSD License)
*                               For Smart Surveillance Framework
*                                 http://ssig.dcc.ufmg.br/ssf/
*
*  Copyright (c) 2013, Smart Surveillance Interest Group, all rights reserved.
*
*  Redistribution and use in source and binary forms, with or without modification, are permitted
*  provided that the following conditions are met:
*
*    1. Redistributions of source code must retain the above copyright notice, this list of
*       conditions and the following disclaimer.
*
*    2. Redistributions in binary form must reproduce the above copyright notice, this list of
*       conditions and the following disclaimer in the documentation and/or other materials
*       provided with the distribution.
*
*    3. Neither the name of the copyright holder nor the names of its contributors may be used to
*       endorse or promote products derived from this software without specific prior written
*       permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
*  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
*  AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
*  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
*  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
*  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*************************************************************************************************L*/

#include "algorithms/pls_image_clustering.hpp"
#include <memory>

namespace ssf{

void PLSImageClustering::predict(cv::Mat_<float>& inp,
                                 cv::Mat_<float>& resp) const{
  resp.release();
  mClassifier->predict(inp, resp);
}


bool PLSImageClustering::empty() const{
  return mClassifier->empty();
}


bool PLSImageClustering::isTrained() const{
  return mClassifier->isTrained();
}


bool PLSImageClustering::isClassifier() const{
  return false;
}


void PLSImageClustering::getCentroids(cv::Mat_<float>& centroidsMatrix) const{
  cv::Mat_<float> centroids;
  buildClusterRepresentation(mSamples, mClusters, centroids);
  centroidsMatrix = centroids;
}

void PLSImageClustering::setClassifier(Classification& classifier){
  mClassifier = std::unique_ptr<OAAClassifier>
    (static_cast<OAAClassifier*>(classifier.clone()));
}

void PLSImageClustering::setClusterRepresentationType(ClusterRepresentationType type){
  this->mRepresentationType = type;
}


void PLSImageClustering::load(const std::string& filename, const std::string& nodename){
  cv::FileStorage fs(filename, cv::FileStorage::READ);
  auto node = fs[nodename];
  read(node);
}


void PLSImageClustering::save(const std::string& filename, const std::string& nodename) const{
  cv::FileStorage fs(filename, cv::FileStorage::WRITE);
  fs << nodename << "{";
  write(fs);
  fs << "}";
}

void PLSImageClustering::read(const cv::FileNode& fn){
  auto node = fn["Classifier"];
  mClassifier->read(node);
}

void PLSImageClustering::write(cv::FileStorage& fs) const{
  fs << "Classifier" << "{";
  mClassifier->write(fs);
  fs << "}";
}

void PLSImageClustering::precondition(){}


void PLSImageClustering::initializeClusterings(
  const std::vector<int>& assignmentSet){
  if(mClusters.empty()){
    //Make the standard initial Clustering
    std::vector<int> chosen;
    for(int i = 0; i < static_cast<int>(assignmentSet.size()); ++i){
      chosen.push_back(i);
    }
    std::shuffle(chosen.begin(), chosen.end(), std::default_random_engine());
    for(int i = 0; i < mInitialK; ++i){
      mClusters.push_back({chosen[i]});
      mClustersIds.push_back(i);
    }
  } else{
    for(int i = 0; i < static_cast<int>(mClusters.size()); ++i){
      mClustersIds.push_back(i);
    }
  }
  // The following is still experimental it was not used in the sibgrapi paper
  //merge(mClusters);
}


void PLSImageClustering::initializeClassifiers(){}


void PLSImageClustering::trainClassifiers(const cv::Mat_<float>& samples,
                                          const std::vector<Cluster>& clusters,
                                          const std::vector<int>& negativeLearningSet){
  trainClassifiers(clusters, negativeLearningSet, (*mClassifier));
}


void PLSImageClustering::trainClassifiers(
  const std::vector<Cluster>& clusters,
  const std::vector<int>& negativeLearningSet,
  OAAClassifier& classifier) const{
  cv::Mat_<float> inp;
  cv::Mat_<int> labels;
  int label = 0;
  for(auto& cluster : clusters){
    ++label;
    for(auto id : cluster){
      inp.push_back(mSamples.row(id));
      labels.push_back(label);
    }
  }

  classifier.learn(inp, labels);
}


bool PLSImageClustering::isFinished(){
  if(getMaxIterations() > 0 && (mIt > getMaxIterations())){
    printf("Convergence due to max number of iterations reached\n");
    return true;
  }
  if(getK()){
    auto kConvergence = (static_cast<int>(mNewClusters.size()) <= getK());
    if(kConvergence){
      printf("Converged due to minimum K!\n");
      return true;
    }
  }
  if(!mMergeOcurred){
    return true;
  }
  return false;
}


void PLSImageClustering::postCondition(){}


void PLSImageClustering::assignment(const cv::Mat_<float>& samples, const int clusterSize, const int nClusters, const std::vector<int>& assignmentSet, std::vector<std::vector<float>>& clustersResponses, std::vector<int>& clustersIds, std::vector<Cluster>& out){

  const int C = static_cast<int>(MIN(nClusters, assignmentSet.size() / clusterSize));
  const int nLabels = static_cast<int>(mClassifier->getLabelsOrdering().size());
  std::unordered_map<int, bool> pointAvailability;
  std::vector<Cluster> clusters;
  clustersResponses.clear();
  std::vector<int> ids;
  cv::Mat_<float> responsesMatrix(
    static_cast<int>(assignmentSet.size()), nLabels);

  for(int sample = 0; sample < static_cast<int>(assignmentSet.size()); ++sample){
    cv::Mat_<float> response;
    cv::Mat_<float> feat = mSamples.row(assignmentSet[sample]);
    mClassifier->predict(feat, response);
    response.copyTo(responsesMatrix.row(sample));
  }
  cv::transpose(responsesMatrix, responsesMatrix);

  cv::Mat_<int> ordering;
  cv::sortIdx(responsesMatrix, ordering, cv::SORT_DESCENDING + cv::SORT_EVERY_ROW);

  //Loop consists of two steps
  // calculating the sum of responses
  // picking the fittest cluster
  int nAssignment = 0;
  std::vector<bool> clusterAssigned(nClusters, false);
  while(nAssignment < C){
    Cluster newCluster;
    //step one:
    float maxSum = -FLT_MAX;
    int chosenClusterId = -1;
    for(int clusterId = 0; clusterId < nClusters; ++clusterId){
      if(clusterAssigned[clusterId])continue;

      int m = 0;
      int i = 0;
      float sum = 0;
      Cluster clusterSet;
      clusterSet.reserve(clusterSize);
      do{
        int sampleId = assignmentSet[ordering[clusterId][i]];
        auto it = pointAvailability.find(sampleId);
        bool availability = (it == pointAvailability.end()) || it->second;
        if(availability){
          sum += responsesMatrix[clusterId][ordering[clusterId][i]];
          clusterSet.push_back(sampleId);
          ++m;
        }
        ++i;
      } while(m < clusterSize);
      if(sum > maxSum){
        maxSum = sum;
        chosenClusterId = clusterId;
        newCluster = clusterSet;
      }
    }

    //step two
    clusterAssigned[chosenClusterId] = true;
    clustersResponses.push_back({});
    ids.push_back(clustersIds[chosenClusterId]);
    for(int p = 0; p < static_cast<int>(newCluster.size()); p++){
      clustersResponses[clusters.size()].push_back(0);
    }
    clusters.push_back(newCluster);
    for(auto& sampleId : newCluster){
      pointAvailability[sampleId] = false;
    }
    nAssignment++;
  }
  clustersIds = ids;

  //MERGING
  merge(clusters);
  out = clusters;
}


void PLSImageClustering::buildClusterRepresentation(
  const cv::Mat_<float>& samples,
  const std::vector<std::vector<int>>& clusters,
  cv::Mat_<float>& clusterRepresentation) const{
  if(mRepresentationType == Centroids){
    const int dimensions = samples.cols;
    clusterRepresentation.create(static_cast<int>(clusters.size()), dimensions);
    int i = 0;
    for(auto cluster : clusters){
      cv::Mat_<float> sample(1, dimensions);
      sample = 0;
      for(auto id : cluster){
        sample = sample + samples.row(id);
      }
      cv::Mat_<float> value = (sample / static_cast<int>(cluster.size()));
      value.copyTo(clusterRepresentation.row(i));
      ++i;
    }
  } else if(mRepresentationType == ClustersResponses){
    auto classifier = std::unique_ptr<OAAClassifier>(
      static_cast<OAAClassifier*>(mClassifier->clone()));
    trainClassifiers(clusters, mNatural[0], *classifier);
    const int dimensions = static_cast<int>(clusters.size());

    clusterRepresentation.create(samples.rows, dimensions);
    clusterRepresentation = 0;
    for(int r = 0; r < samples.rows; ++r){
      cv::Mat_<float> resp;
      cv::Mat_<float> feat = samples.row(r);
      classifier->predict(feat, resp);
      resp.copyTo(clusterRepresentation.row(r));
    }
    cv::transpose(clusterRepresentation, clusterRepresentation);
  }
}

std::shared_ptr<OAAClassifier> PLSImageClustering::getClassifier() const{
  return std::shared_ptr<OAAClassifier>(dynamic_cast<OAAClassifier*>(mClassifier->clone()));
}

std::function<float(cv::Mat_<float>&, cv::Mat_<float>&)> PLSImageClustering::getSimBuilder() const{
  return mSimilarityFunction;
}

void PLSImageClustering::setSimBuilder(const std::function<float(cv::Mat_<float>&, cv::Mat_<float>&)>& function){

  mSimilarityFunction = function;
}

int PLSImageClustering::getRepresentationType() const{
  return mRepresentationType;
}

void PLSImageClustering::setRepresentationType(int mRepresentationType1){
  mRepresentationType = mRepresentationType1;
}

float PLSImageClustering::getMergeThreshold() const{
  return mMergeThreshold;
}

void PLSImageClustering::setMergeThreshold(float mMergeThreshold1){
  mMergeThreshold = mMergeThreshold1;
}

void PLSImageClustering::merge(std::vector<Cluster>& clusters){
  mMergeOcurred = false;

  bool hasMerged = false;
  std::vector<Cluster> ans;
  do{
    if(static_cast<int>(clusters.size() + ans.size()) <= mK){
      break;
    }
    cv::Mat_<float> clusterRepresentation;
    buildClusterRepresentation(mSamples, clusters, clusterRepresentation);

    cv::Mat_<float> similarity = SimilarityBuilder::
      buildSimilarity(clusterRepresentation, mSimilarityFunction);

    //similarity = cv::abs(similarity);
    std::pair<int, int> mergedPair;
    hasMerged = findClosestClusters(similarity, mMergeThreshold, mergedPair);
    if(hasMerged){
      mMergeOcurred = true;
      int id1 = mergedPair.first;
      int id2 = mergedPair.second;

      clusters[id1].insert(clusters[id1].end(),
                           clusters[id2].begin(),
                           clusters[id2].end());

      Cluster mergeResult;
      mergeResult.insert(mergeResult.end(), clusters[id1].begin(),
                         clusters[id1].end());

      clusters.erase(clusters.begin() + id2);
      clusters.erase(clusters.begin() + id1);

      ans.push_back(mergeResult);
    }

    similarity.release();
  } while(hasMerged);
  if(clusters.size() > 0){
    ans.insert(ans.end(), clusters.begin(), clusters.end());
  }
  clusters = ans;
}


bool PLSImageClustering::findClosestClusters(
  const cv::Mat& similarityMatrix,
  const float threshold,
  std::pair<int, int>& closestPair){
  double max = 0;
  int idxs[] = {0, 0};
  cv::minMaxIdx(similarityMatrix, nullptr, &max, nullptr, idxs);
  closestPair = std::make_pair(idxs[0], idxs[1]);
  return max >= threshold;
}
}
