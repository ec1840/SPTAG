// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "inc/Core/KDT/Index.h"

#pragma warning(disable:4996)  // 'fopen': This function or variable may be unsafe. Consider using fopen_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
#pragma warning(disable:4242)  // '=' : conversion from 'int' to 'short', possible loss of data
#pragma warning(disable:4244)  // '=' : conversion from 'int' to 'short', possible loss of data
#pragma warning(disable:4127)  // conditional expression is constant

namespace SPTAG
{
    namespace KDT
    {
        template <typename T>
        ErrorCode Index<T>::LoadIndexFromMemory(const std::vector<void*>& p_indexBlobs)
        {
            if (!m_pSamples.Load((char*)p_indexBlobs[0])) return ErrorCode::FailedParseValue;
            if (!m_pTrees.LoadTrees((char*)p_indexBlobs[1])) return ErrorCode::FailedParseValue;
            if (!m_pGraph.LoadGraph((char*)p_indexBlobs[2])) return ErrorCode::FailedParseValue;
            return ErrorCode::Success;
        }

        template <typename T>
        ErrorCode Index<T>::LoadIndex(const std::string& p_folderPath, Helper::IniReader& p_reader)
        {
#define DefineKDTParameter(VarName, VarType, DefaultValue, RepresentStr) \
            SetParameter(RepresentStr, \
                         p_reader.GetParameter("Index", \
                         RepresentStr, \
                         std::string(#DefaultValue)).c_str()); \

#include "inc/Core/KDT/ParameterDefinitionList.h"
#undef DefineKDTParameter

            if (!m_pSamples.Load(p_folderPath + m_sDataPointsFilename)) return ErrorCode::Fail;
            if (!m_pTrees.LoadTrees(p_folderPath + m_sKDTFilename)) return ErrorCode::Fail;
            if (!m_pGraph.LoadGraph(p_folderPath + m_sGraphFilename)) return ErrorCode::Fail;

            m_workSpacePool.reset(new COMMON::WorkSpacePool(m_iMaxCheck, GetNumSamples()));
            m_workSpacePool->Init(m_iNumberOfThreads);
            return ErrorCode::Success;
        }

#pragma region K-NN search

#define Search(CheckDeleted1) \
        m_pTrees.InitSearchTrees(this, p_query, p_space, m_iNumberOfInitialDynamicPivots); \
        while (!p_space.m_NGQueue.empty()) { \
            COMMON::HeapCell gnode = p_space.m_NGQueue.pop(); \
            const int *node = m_pGraph[gnode.node]; \
            _mm_prefetch((const char *)node, _MM_HINT_T0); \
            CheckDeleted1 { \
                if (!p_query.AddPoint(gnode.node, gnode.distance) && p_space.m_iNumberOfCheckedLeaves > p_space.m_iMaxCheck) { \
                    p_query.SortResult(); return; \
                } \
            } \
            for (int i = 0; i < m_pGraph.m_iNeighborhoodSize; i++) \
                _mm_prefetch((const char *)(m_pSamples)[node[i]], _MM_HINT_T0); \
            float upperBound = max(p_query.worstDist(), gnode.distance); \
            bool bLocalOpt = true; \
            for (int i = 0; i < m_pGraph.m_iNeighborhoodSize; i++) { \
                int nn_index = node[i]; \
                if (nn_index < 0) break; \
                if (p_space.CheckAndSet(nn_index)) continue; \
                float distance2leaf = m_fComputeDistance(p_query.GetTarget(), (m_pSamples)[nn_index], GetFeatureDim()); \
                if (distance2leaf <= upperBound) bLocalOpt = false; \
                p_space.m_iNumberOfCheckedLeaves++; \
                p_space.m_NGQueue.insert(COMMON::HeapCell(nn_index, distance2leaf)); \
            } \
            if (bLocalOpt) p_space.m_iNumOfContinuousNoBetterPropagation++; \
            else p_space.m_iNumOfContinuousNoBetterPropagation = 0; \
            if (p_space.m_iNumOfContinuousNoBetterPropagation > m_iThresholdOfNumberOfContinuousNoBetterPropagation) { \
                if (p_space.m_iNumberOfTreeCheckedLeaves <= p_space.m_iNumberOfCheckedLeaves / 10) { \
                    m_pTrees.SearchTrees(this, p_query, p_space, m_iNumberOfOtherDynamicPivots + p_space.m_iNumberOfCheckedLeaves); \
                } else if (gnode.distance > p_query.worstDist()) { \
                    break; \
                } \
            } \
        } \
        p_query.SortResult(); \

        template <typename T>
        void Index<T>::SearchIndexWithDeleted(COMMON::QueryResultSet<T> &p_query, COMMON::WorkSpace &p_space, const COMMON::ConcurrentSet<int> &p_deleted) const
        {
            Search(if (!p_deleted.contains(gnode.node)))
        }

        template <typename T>
        void Index<T>::SearchIndexWithoutDeleted(COMMON::QueryResultSet<T> &p_query, COMMON::WorkSpace &p_space) const
        {
            Search(;)
        }

        template<typename T>
        ErrorCode
            Index<T>::SearchIndex(QueryResult &p_query) const
        {
            auto workSpace = m_workSpacePool->Rent();
            workSpace->Reset(m_iMaxCheck);

            if (m_deletedID.size() > 0)
                SearchIndexWithDeleted(*((COMMON::QueryResultSet<T>*)&p_query), *workSpace, m_deletedID);
            else
                SearchIndexWithoutDeleted(*((COMMON::QueryResultSet<T>*)&p_query), *workSpace);

            m_workSpacePool->Return(workSpace);

            if (p_query.WithMeta() && nullptr != m_pMetadata)
            {
                for (int i = 0; i < p_query.GetResultNum(); ++i)
                {
                    int result = p_query.GetResult(i)->VID;
                    p_query.SetMetadata(i, (result < 0) ? ByteArray::c_empty : m_pMetadata->GetMetadata(result));
                }
            }
            return ErrorCode::Success;
        }
#pragma endregion

        template <typename T>
        ErrorCode Index<T>::BuildIndex(const void* p_data, int p_vectorNum, int p_dimension)
        {
            omp_set_num_threads(m_iNumberOfThreads);

            m_pSamples.Initialize(p_vectorNum, p_dimension, (T*)p_data, false);

            if (DistCalcMethod::Cosine == m_iDistCalcMethod)
            {
                int base = COMMON::Utils::GetBase<T>();
#pragma omp parallel for
                for (int i = 0; i < GetNumSamples(); i++) {
                    COMMON::Utils::Normalize(m_pSamples[i], GetFeatureDim(), base);
                }
            }

            m_workSpacePool.reset(new COMMON::WorkSpacePool(m_iMaxCheck, GetNumSamples()));
            m_workSpacePool->Init(m_iNumberOfThreads);

            m_pTrees.BuildTrees<T>(this);
            m_pGraph.BuildGraph<T>(this);
            
            return ErrorCode::Success;
        }

        template <typename T>
        ErrorCode Index<T>::RefineIndex(const std::string& p_folderPath)
        {
            std::string folderPath(p_folderPath);
            if (!folderPath.empty() && *(folderPath.rbegin()) != FolderSep)
            {
                folderPath += FolderSep;
            }

            if (!direxists(folderPath.c_str()))
            {
                mkdir(folderPath.c_str());
            }

            std::lock_guard<std::mutex> lock(m_dataAddLock);
            std::shared_lock<std::shared_timed_mutex> sharedlock(m_deletedID.getLock());

            int newR = GetNumSamples();

            std::vector<int> indices;
            std::vector<int> reverseIndices(newR);
            for (int i = 0; i < newR; i++) {
                if (!m_deletedID.contains(i)) {
                    indices.push_back(i);
                    reverseIndices[i] = i;
                }
                else {
                    while (m_deletedID.contains(newR - 1) && newR > i) newR--;
                    if (newR == i) break;
                    indices.push_back(newR - 1);
                    reverseIndices[newR - 1] = i;
                    newR--;
                }
            }

            std::cout << "Refine... from " << GetNumSamples() << "->" << newR << std::endl;

            if (false == m_pSamples.Refine(indices, folderPath + m_sDataPointsFilename)) return ErrorCode::FailedCreateFile;
            if (nullptr != m_pMetadata && ErrorCode::Success != m_pMetadata->RefineMetadata(indices, folderPath)) return ErrorCode::FailedCreateFile;
            
            m_pGraph.RefineGraph<T>(this, indices, reverseIndices, folderPath + m_sGraphFilename);
            
            COMMON::KDTree newTrees(m_pTrees);
            newTrees.BuildTrees<T>(this, &indices);
#pragma omp parallel for
            for (int i = 0; i < newTrees.size(); i++) {
                if (newTrees[i].left < 0)
                    newTrees[i].left = -reverseIndices[-newTrees[i].left - 1] - 1;
                if (newTrees[i].right < 0)
                    newTrees[i].right = -reverseIndices[-newTrees[i].right - 1] - 1;
            }
            newTrees.SaveTrees(folderPath + m_sKDTFilename);

            return ErrorCode::Success;
        }

        template <typename T>
        ErrorCode Index<T>::DeleteIndex(const void* p_vectors, int p_vectorNum) {
            const T* ptr_v = (const T*)p_vectors;
#pragma omp parallel for schedule(dynamic)
            for (int i = 0; i < p_vectorNum; i++) {
                COMMON::QueryResultSet<T> query(ptr_v + i * GetFeatureDim(), m_pGraph.m_iCEF);
                SearchIndex(query);

                for (int i = 0; i < m_pGraph.m_iCEF; i++) {
                    if (query.GetResult(i)->Dist < 1e-6) {
                        m_deletedID.insert(query.GetResult(i)->VID);
                    }
                }
            }
            return ErrorCode::Success;
        }

        template <typename T>
        ErrorCode Index<T>::AddIndex(const void* p_vectors, int p_vectorNum, int p_dimension) 
        {
            int begin, end;
            {
                std::lock_guard<std::mutex> lock(m_dataAddLock);

                if (GetNumSamples() == 0)
                    return BuildIndex(p_vectors, p_vectorNum, p_dimension);

                if (p_dimension != GetFeatureDim())
                    return ErrorCode::FailedParseValue;

                begin = GetNumSamples();
                end = GetNumSamples() + p_vectorNum;

                if (m_pSamples.AddBatch((const T*)p_vectors, p_vectorNum) != ErrorCode::Success || m_pGraph.AddBatch(p_vectorNum) != ErrorCode::Success) {
                    std::cout << "Memory Error: Cannot alloc space for vectors" << std::endl;
                    m_pSamples.SetR(begin);
                    m_pGraph.SetR(begin);
                    return ErrorCode::MemoryOverFlow;
                }
                if (DistCalcMethod::Cosine == m_iDistCalcMethod)
                {
                    int base = COMMON::Utils::GetBase<T>();
                    for (int i = begin; i < end; i++) {
                        COMMON::Utils::Normalize((T*)m_pSamples[i], GetFeatureDim(), base);
                    }
                }
            }

            for (int node = begin; node < end; node++)
            {
                m_pGraph.RefineNode<T>(this, node, true);
            }
            std::cout << "Add " << p_vectorNum << " vectors" << std::endl;
            return ErrorCode::Success;
        }

        template<typename T>
        ErrorCode
            Index<T>::SaveIndex(const std::string& p_folderPath, std::ofstream& p_configout)
        {
            m_sDataPointsFilename = "vectors.bin";
            m_sKDTFilename = "tree.bin";
            m_sGraphFilename = "graph.bin";

#define DefineKDTParameter(VarName, VarType, DefaultValue, RepresentStr) \
    p_configout << RepresentStr << "=" << GetParameter(RepresentStr) << std::endl;

#include "inc/Core/KDT/ParameterDefinitionList.h"
#undef DefineKDTParameter

            p_configout << std::endl;

            if (m_deletedID.size() > 0) {
                RefineIndex(p_folderPath);
            }
            else {
                if (!m_pSamples.Save(p_folderPath + m_sDataPointsFilename)) return ErrorCode::Fail;
                if (!m_pTrees.SaveTrees(p_folderPath + m_sKDTFilename)) return ErrorCode::Fail;
                if (!m_pGraph.SaveGraph(p_folderPath + m_sGraphFilename)) return ErrorCode::Fail;
            }
            return ErrorCode::Success;
        }

        template <typename T>
        ErrorCode
            Index<T>::SetParameter(const char* p_param, const char* p_value)
        {
            if (nullptr == p_param || nullptr == p_value) return ErrorCode::Fail;

#define DefineKDTParameter(VarName, VarType, DefaultValue, RepresentStr) \
    else if (SPTAG::Helper::StrUtils::StrEqualIgnoreCase(p_param, RepresentStr)) \
    { \
        fprintf(stderr, "Setting %s with value %s\n", RepresentStr, p_value); \
        VarType tmp; \
        if (SPTAG::Helper::Convert::ConvertStringTo<VarType>(p_value, tmp)) \
        { \
            VarName = tmp; \
        } \
    } \

#include "inc/Core/KDT/ParameterDefinitionList.h"
#undef DefineKDTParameter

            m_fComputeDistance = COMMON::DistanceCalcSelector<T>(m_iDistCalcMethod);
            return ErrorCode::Success;
        }


        template <typename T>
        std::string
            Index<T>::GetParameter(const char* p_param) const
        {
            if (nullptr == p_param) return std::string();

#define DefineKDTParameter(VarName, VarType, DefaultValue, RepresentStr) \
    else if (SPTAG::Helper::StrUtils::StrEqualIgnoreCase(p_param, RepresentStr)) \
    { \
        return SPTAG::Helper::Convert::ConvertToString(VarName); \
    } \

#include "inc/Core/KDT/ParameterDefinitionList.h"
#undef DefineKDTParameter

            return std::string();
        }
    }
}

#define DefineVectorValueType(Name, Type) \
template class SPTAG::KDT::Index<Type>; \

#include "inc/Core/DefinitionList.h"
#undef DefineVectorValueType


