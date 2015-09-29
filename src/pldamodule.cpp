#include <Python.h>
#include <iostream>
#include <string>
#include <iterator>
#include <vector>
#include <algorithm>
//numpy library
#include "numpy/arrayobject.h"
// T_INT and all other datatypes for the class
#include "structmember.h"
#include <cassert>



#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "matrix/matrix-lib.h"

#include "chtk.h"
#include "ivector/plda.h"
#include "kaldi-utils.hpp"

namespace kaldi{


    typedef struct {
        PyObject_HEAD
        Plda plda;
        PldaEstimationConfig estconfig;
        PldaConfig config;
    } MPlda;

    struct Stats{
        uint32_t size;
        Vector<double> data;
    };


    static PyObject * MPlda_fit(MPlda* self, PyObject *args, PyObject * kwds){
        SetVerboseLevel(0);
        //From the given data as features with dimensions (nsamples,featdim ) and labels as (nsamples,)
        //we estimate the statistics.
        //The labels values indicate which label is given for each sample in nsamples
        PyArrayObject* py_inputfeats;
        PyArrayObject* py_labels;
        // Default number of iterations is 10
        uint32_t iters=10;
        if (! PyArg_ParseTuple( args, "O!O!|O!O!k", &PyArray_Type,&py_inputfeats,&PyArray_Type,&py_labels,&iters)) return NULL;

        auto n_samples=py_inputfeats->dimensions[0];
        auto featdim =py_inputfeats->dimensions[1];

        assert(py_labels->dimensions[0]==py_inputfeats->dimensions[0]);

        PldaStats stats;

        const Matrix<double> &inputfeats = pyarraytomatrix<double>(py_inputfeats);

        const long *labels = pyvector_to_type<long>(py_labels);

        std::set<long> u_labels;
        for (auto sample = 0u; sample < n_samples; ++sample) {
            u_labels.insert(labels[sample]);
        }
        auto num_speakers = u_labels.size();
        std::vector<MatrixIndexT> indices[num_speakers];
        for(auto n=0u ; n < n_samples;n++){
            indices[labels[n]].push_back(n);
        }

        for(auto spk=0u; spk < num_speakers;spk ++){
            Matrix<double> tmp(indices[spk].size(),featdim);
            tmp.CopyRows(inputfeats,indices[spk]);
            stats.AddSamples(1.0/indices[spk].size(),tmp);
        }

        stats.Sort();

        self->estconfig.num_em_iters = iters;
        PldaEstimator estimator(stats);

        // Plda plda;
        estimator.Estimate(self->estconfig,&(self->plda));
        return Py_BuildValue("");
    }

    static PyObject* Mplda_transform(MPlda* self,PyObject* args,PyObject* kwads){

        float smoothfactor;
        PyArrayObject* py_inpututts;
        PyArrayObject* py_labels;
        uint32_t targetdim = 0;
        if (! PyArg_ParseTuple( args, "O!O!|O!O!k", &PyArray_Type,&py_inpututts,&PyArray_Type,&py_labels,&targetdim)) return NULL;
        std::map<uint32_t,Stats> speakertoutts;
        PyObject *retdict = PyDict_New();

        const Matrix<double> &inputfeats = pyarraytomatrix<double>(py_inpututts);
        auto n_samples=py_inpututts->dimensions[0];
        auto featdim =py_inpututts->dimensions[1];
        // If targetdim is given, we transform the vectors to targetdim
        if (targetdim>0){
            featdim = targetdim;
        }

        assert(py_labels->dimensions[0]==py_inpututts->dimensions[0]);
        long *labels = pyvector_to_type<long>(py_labels);

        // Get the unique labels in the number of labels, which are the speakers
        std::set<long> u_labels;
        for (auto sample = 0u; sample < n_samples; ++sample) {
            u_labels.insert(labels[sample]);
        }

        auto num_speakers = u_labels.size();

        for(auto spk = 0u; spk < num_speakers;spk++){
            if(speakertoutts.count(spk)==0){
                Stats stat;
                stat.size=0;
                stat.data.Resize(featdim);
                speakertoutts.insert(std::make_pair(spk,stat));
            }
            speakertoutts[labels[spk]].size += 1;
            speakertoutts[labels[spk]].data.AddVec(1.0,inputfeats.Row(spk));
        }

        for(std::map<uint32_t,Stats>::iterator it=speakertoutts.begin();it!=speakertoutts.end();it++){
            auto samplesize=it->second.size;
            assert(samplesize>0);
            // Scale the vector by its samplesize to normalize it
            it->second.data.Scale(1.0/samplesize);
            Vector<double> transformed(featdim);
            self->plda.TransformIvector(self->config,it->second.data,&transformed);

            PyObject* spkid = PyInt_FromSize_t(it->first);
            npy_intp dimensions[1]  = {transformed.Dim()};
            std::cerr << transformed <<std::endl;
            PyArrayObject* py_transformed = (PyArrayObject* )PyArray_SimpleNewFromData(2,dimensions,NPY_DOUBLE,transformed.Data());
            py_transformed->flags |= NPY_ARRAY_OWNDATA;
            // Set the value in the map as the transformed value
            PyDict_SetItem(retdict,spkid,(PyObject*) py_transformed);
        }
        return Py_BuildValue("O",retdict);


    }


    static PyMethodDef Plda_methods[] = {
        {"fit", (PyCFunction)MPlda_fit, METH_VARARGS,
         "Fit the model with parameters X,Y, numiters\n"
         "X = (nsamples,featdim) and Y = (nsamples,) , numiters = number of iterations for PLDA estimation"
        },
        {"transform",(PyCFunction)Mplda_transform,METH_VARARGS,
        "Transforms the given parameters X into the PLDA subspace. "
        },
        {NULL}  /* Sentinel */
    };

    static PyObject *
    Plda_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
    {
        MPlda *self;

        self = (MPlda *)type->tp_alloc(type, 0);

        return (PyObject *)self;
    }



    static PyTypeObject MPlda_Type = {
        PyObject_HEAD_INIT(NULL)
        0,                         /*ob_size*/
        "libplda.Mplda",             /*tp_name*/
        sizeof(MPlda),             /*tp_basicsize*/
        0,                         /*tp_itemsize*/
        0, /*tp_dealloc*/
        0,                         /*tp_print*/
        0,                         /*tp_getattr*/
        0,                         /*tp_setattr*/
        0,                         /*tp_compare*/
        0,                         /*tp_repr*/
        0,                         /*tp_as_number*/
        0,                         /*tp_as_sequence*/
        0,                         /*tp_as_mapping*/
        0,                         /*tp_hash */
        0,                         /*tp_call*/
        0,                         /*tp_str*/
        0,                         /*tp_getattro*/
        0,                         /*tp_setattro*/
        0,                         /*tp_as_buffer*/
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
        "Plda object",           /* tp_doc */
        0,                     /* tp_traverse */
        0,                     /* tp_clear */
        0,                     /* tp_richcompare */
        0,                     /* tp_weaklistoffset */
        0,                     /* tp_iter */
        0,                     /* tp_iternext */
        Plda_methods,             /* tp_methods */
        0,                /* tp_members */
        0,           /* tp_getset */
        0,                         /* tp_base */
        0,                         /* tp_dict */
        0,                         /* tp_descr_get */
        0,                         /* tp_descr_set */
        0,                         /* tp_dictoffset */
        0,      /* tp_init */
        0,                         /* tp_alloc */
        0,                 /* tp_new */
    };


    static PyMethodDef libpldaModule_methods1[] = {
        {NULL}
    };


    extern "C" {
        #ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
        #define PyMODINIT_FUNC void
        #endif
        PyMODINIT_FUNC initlibplda(){
        // (void) Py_InitModule("libplda",libpldaModule_methods);
        if (PyType_Ready(&MPlda_Type) < 0)
            return;
        MPlda_Type.tp_new = PyType_GenericNew;
        PyObject *m = Py_InitModule3("libplda", libpldaModule_methods1,
                       "Example module that creates an extension type.");
        if (m == NULL)
            return;
        import_array();

        Py_INCREF(&MPlda_Type);
        PyModule_AddObject(m, "MPlda", (PyObject *)&MPlda_Type);

        }
    }

}