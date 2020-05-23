// nnet3bin/nnet3-chain-train.cc

// Copyright 2015  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "nnet3/nnet-chain-adapt.h"
#include "cudamatrix/cu-allocator.h"

int main(int argc, char* argv[]) {
    try {
        using namespace kaldi;
        using namespace kaldi::nnet3;
        using namespace kaldi::chain;
        typedef kaldi::int32 int32;
        typedef kaldi::int64 int64;

        const char* usage =
            "Adapt nnet3+chain neural network parameters with backprop and stochastic\n"
            "gradient descent.  Minibatches are to be created by nnet3-chain-merge-egs in\n"
            "the input pipeline.  This training program is single-threaded (best to\n"
            "use it with a GPU).\n"
            "\n"
            "Usage:  nnet3-chain-adapt [options] <raw-nnet-in> <si-raw_nnet-in> <denominator-fst-in> <chain-training-examples-in> <raw-nnet-out>\n"
            "\n"
            "nnet3-chain-adapt 1.raw si.raw den.fst 'ark:nnet3-merge-egs 1.cegs ark:-|' 2.raw\n";

        int32 srand_seed = 0;
        bool binary_write = true;
        std::string use_gpu = "yes";
        NnetChainAdaptationOptions opts;

        ParseOptions po(usage);
        po.Register("srand", &srand_seed, "Seed for random number generator ");
        po.Register("binary", &binary_write, "Write output in binary mode");
        po.Register("use-gpu", &use_gpu,
            "yes|no|optional|wait, only has effect if compiled with CUDA");

        opts.Register(&po);
#if HAVE_CUDA==1
        CuDevice::RegisterDeviceOptions(&po);
#endif
        RegisterCuAllocatorOptions(&po);

        po.Read(argc, argv);

        srand(srand_seed);

        if (po.NumArgs() != 5) {
            po.PrintUsage();
            exit(1);
        }

#if HAVE_CUDA==1
        CuDevice::Instantiate().SelectGpuId(use_gpu);
#endif

        std::string nnet_rxfilename = po.GetArg(1),
            si_nnet_rxfilename = po.GetArg(2),
            den_fst_rxfilename = po.GetArg(3),
            examples_rspecifier = po.GetArg(4),
            nnet_wxfilename = po.GetArg(5);

        Nnet nnet;
        ReadKaldiObject(nnet_rxfilename, &nnet);

        Nnet si_nnet;
        ReadKaldiObject(si_nnet_rxfilename, &si_nnet);

        bool ok;

        {
            fst::StdVectorFst den_fst;
            ReadFstKaldi(den_fst_rxfilename, &den_fst);

            NnetChainAdapter adapter(opts, den_fst, &nnet, &si_nnet);

            SequentialNnetChainExampleReader example_reader(examples_rspecifier);

            for (; !example_reader.Done(); example_reader.Next())
                adapter.Train(example_reader.Value());

            ok = adapter.PrintTotalStats();
        }

#if HAVE_CUDA==1
        CuDevice::Instantiate().PrintProfile();
#endif
        WriteKaldiObject(nnet, nnet_wxfilename, binary_write);
        KALDI_LOG << "Wrote raw model to " << nnet_wxfilename;
        return (ok ? 0 : 1);
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return -1;
    }
}
