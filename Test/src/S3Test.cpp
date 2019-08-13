

#include "inc/Test.h"

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/Object.h>


BOOST_AUTO_TEST_SUITE(S3Test)

BOOST_AUTO_TEST_CASE(S3ListTest)
{
    Aws::SDKOptions options;
    Aws::InitAPI(options);

    Aws::String bucket_name = "elasticbeanstalk-us-west-2-037120262777";

    Aws::Client::ClientConfiguration configuration;
    configuration.region = "us-west-2";
    Aws::S3::S3Client s3_client(configuration);

    Aws::S3::Model::ListObjectsRequest objects_request;
    objects_request.WithBucket(bucket_name);

    auto list_objects_outcome = s3_client.ListObjects(objects_request);

    if (list_objects_outcome.IsSuccess())
    {
        Aws::Vector<Aws::S3::Model::Object> object_list =
            list_objects_outcome.GetResult().GetContents();

        for (auto const &s3_object : object_list)
        {
            std::cout << "* " << s3_object.GetKey() << std::endl;
        }
    }
    else
    {
        std::cout << "ListObjects error: " <<
            list_objects_outcome.GetError().GetExceptionName() << " " <<
            list_objects_outcome.GetError().GetMessage() << std::endl;
    }


    Aws::ShutdownAPI(options);
    std::cout << "S3 Load OK!" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()