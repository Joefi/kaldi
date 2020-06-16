
all:

include ../kaldi.mk

LDFLAGS += $(CUDA_LDFLAGS)
LDLIBS += $(CUDA_LDLIBS)

BINFILES = server-tcp-nnet3-decode-faster server-tcp-nnet3-adaptation server-tcp-fileupload server-tcp-nnet3-target-speaker-decode server-tcp-model-list

OBJFILES =

TESTFILES =

ADDLIBS = ../online2/kaldi-online2.a ../ivector/kaldi-ivector.a \
          ../nnet3/kaldi-nnet3.a ../chain/kaldi-chain.a ../nnet2/kaldi-nnet2.a \
          ../cudamatrix/kaldi-cudamatrix.a ../decoder/kaldi-decoder.a \
          ../lat/kaldi-lat.a ../fstext/kaldi-fstext.a ../hmm/kaldi-hmm.a \
          ../feat/kaldi-feat.a ../transform/kaldi-transform.a \
          ../gmm/kaldi-gmm.a ../tree/kaldi-tree.a ../util/kaldi-util.a \
          ../matrix/kaldi-matrix.a ../base/kaldi-base.a 
include ../makefiles/default_rules.mk
