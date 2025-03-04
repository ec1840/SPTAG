// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef _SPTAG_BKT_INDEX_H_
#define _SPTAG_BKT_INDEX_H_

#include "../Common.h"
#include "../VectorIndex.h"

#include "../Common/CommonUtils.h"
#include "../Common/DistanceUtils.h"
#include "../Common/QueryResultSet.h"
#include "../Common/Dataset.h"
#include "../Common/WorkSpace.h"
#include "../Common/WorkSpacePool.h"
#include "../Common/RelativeNeighborhoodGraph.h"
#include "../Common/BKTree.h"
#include "inc/Helper/ConcurrentSet.h"
#include "inc/Helper/SimpleIniReader.h"
#include "inc/Helper/StringConvert.h"

#include <functional>
#include <mutex>

namespace SPTAG
{

    namespace Helper
    {
        class IniReader;
    }

    namespace BKT
    {
        template<typename T>
        class Index : public VectorIndex
        {
        private:
            // data points
            COMMON::Dataset<T> m_pSamples;
        
            // BKT structures. 
            COMMON::BKTree m_pTrees;

            // Graph structure
            COMMON::RelativeNeighborhoodGraph m_pGraph;

            std::string m_sBKTFilename;
            std::string m_sGraphFilename;
            std::string m_sDataPointsFilename;

            std::mutex m_dataAddLock; // protect data and graph
            COMMON::ConcurrentSet<int> m_deletedID;
            std::unique_ptr<COMMON::WorkSpacePool> m_workSpacePool;

            int m_iNumberOfThreads;
            DistCalcMethod m_iDistCalcMethod;
            float(*m_fComputeDistance)(const T* pX, const T* pY, int length);
        
            int m_iMaxCheck;        
            int m_iThresholdOfNumberOfContinuousNoBetterPropagation;
            int m_iNumberOfInitialDynamicPivots;
            int m_iNumberOfOtherDynamicPivots;
        public:
			Index()
			{
#define DefineBKTParameter(VarName, VarType, DefaultValue, RepresentStr) \
                VarName = DefaultValue; \

#include "inc/Core/BKT/ParameterDefinitionList.h"
#undef DefineBKTParameter

				m_fComputeDistance = COMMON::DistanceCalcSelector<T>(m_iDistCalcMethod);
			}

            ~Index() {}

            inline int GetNumSamples() const { return m_pSamples.R(); }
            inline int GetFeatureDim() const { return m_pSamples.C(); }
        
            inline int GetCurrMaxCheck() const { return m_iMaxCheck; }
            inline int GetNumThreads() const { return m_iNumberOfThreads; }
            inline DistCalcMethod GetDistCalcMethod() const { return m_iDistCalcMethod; }
            inline IndexAlgoType GetIndexAlgoType() const { return IndexAlgoType::BKT; }
            inline VectorValueType GetVectorValueType() const { return GetEnumValueType<T>(); }
            
            inline float ComputeDistance(const void* pX, const void* pY) const { return m_fComputeDistance((const T*)pX, (const T*)pY, m_pSamples.C()); }
            inline const void* GetSample(const int idx) const { return (void*)m_pSamples[idx]; }

            ErrorCode BuildIndex(const void* p_data, int p_vectorNum, int p_dimension);

            ErrorCode LoadIndexFromMemory(const std::vector<void*>& p_indexBlobs);

            ErrorCode SaveIndex(const std::string& p_folderPath, std::ofstream& p_configout);
            ErrorCode LoadIndex(const std::string& p_folderPath, Helper::IniReader& p_reader);
            ErrorCode SearchIndex(QueryResult &p_query) const;
            ErrorCode AddIndex(const void* p_vectors, int p_vectorNum, int p_dimension);
            ErrorCode DeleteIndex(const void* p_vectors, int p_vectorNum);

            ErrorCode SetParameter(const char* p_param, const char* p_value);
            std::string GetParameter(const char* p_param) const;

        private:
            ErrorCode RefineIndex(const std::string& p_folderPath);
            void SearchIndexWithDeleted(COMMON::QueryResultSet<T> &p_query, COMMON::WorkSpace &p_space, const COMMON::ConcurrentSet<int> &p_deleted) const;
            void SearchIndexWithoutDeleted(COMMON::QueryResultSet<T> &p_query, COMMON::WorkSpace &p_space) const;
        };
    } // namespace BKT
} // namespace SPTAG

#endif // _SPTAG_BKT_INDEX_H_
