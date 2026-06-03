/*
 * model_decision.h: The trained micro-sleep decision model, exported to C.
 *
 * GENERATED, then lightly wrapped by hand. Do not edit the scoring logic;
 * regenerate by re-running train_model.py (which calls m2cgen) if the model
 * changes.
 *
 * The model is a depth-6 decision tree (scikit-learn DecisionTreeClassifier)
 * trained on simulator-generated traffic. It answers ONE binary question:
 * given the current polling features, should the loop sleep or keep polling?
 *
 * Feature input order (must match how the caller fills the array):
 *     input[0] = empty_run      consecutive empty polls
 *     input[1] = us_since_pkt   microseconds since last non-empty poll
 *     input[2] = rate_fast      EWMA packets/poll, alpha=0.20
 *     input[3] = rate_med       EWMA packets/poll, alpha=0.02
 *     input[4] = rate_slow      EWMA packets/poll, alpha=0.002
 *
 * score() writes two class probabilities to output[]:
 *     output[0] = P(keep polling)
 *     output[1] = P(sleep)
 * The caller takes the argmax.
 */
#ifndef MODEL_DECISION_H
#define MODEL_DECISION_H

#include <string.h>

/* ---- begin m2cgen-generated scoring function ---- */
void score(double * input, double * output) {
    double var0[2];
    if (input[4] <= 0.00837499974295497) {
        if (input[3] <= 0.004865000024437904) {
            if (input[4] <= 0.005104999989271164) {
                if (input[3] <= 0.00009499999941908754) {
                    if (input[1] <= 902.7000122070312) {
                        if (input[1] <= 104.29999923706055) {
                            memcpy(var0, (double[]){0.1002, 0.8998}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.10631363464957173, 0.8936863653504282}, 2 * sizeof(double));
                        }
                    } else {
                        memcpy(var0, (double[]){1.0, 0.0}, 2 * sizeof(double));
                    }
                } else {
                    if (input[4] <= 0.00161500001559034) {
                        if (input[4] <= 0.0013649999746121466) {
                            memcpy(var0, (double[]){0.08235294117647059, 0.9176470588235294}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.14623837700760778, 0.8537616229923922}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[3] <= 0.0004349999944679439) {
                            memcpy(var0, (double[]){0.10918544194107452, 0.8908145580589255}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.11770416904625929, 0.8822958309537408}, 2 * sizeof(double));
                        }
                    }
                }
            } else {
                if (input[3] <= 0.0010950000141747296) {
                    if (input[3] <= 0.00007499999992433004) {
                        if (input[3] <= 0.000054999998610583134) {
                            memcpy(var0, (double[]){0.1143312101910828, 0.8856687898089172}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.04390243902439024, 0.9560975609756097}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[4] <= 0.008084999863058329) {
                            memcpy(var0, (double[]){0.13333333333333333, 0.8666666666666667}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.25308641975308643, 0.7469135802469136}, 2 * sizeof(double));
                        }
                    }
                } else {
                    if (input[4] <= 0.006245000055059791) {
                        if (input[3] <= 0.001134999969508499) {
                            memcpy(var0, (double[]){0.38095238095238093, 0.6190476190476191}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.16784869976359337, 0.8321513002364066}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[3] <= 0.002624999964609742) {
                            memcpy(var0, (double[]){0.18452380952380953, 0.8154761904761905}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.2434017595307918, 0.7565982404692082}, 2 * sizeof(double));
                        }
                    }
                }
            }
        } else {
            if (input[4] <= 0.005914999870583415) {
                if (input[3] <= 0.020344999618828297) {
                    if (input[4] <= 0.004345000023022294) {
                        if (input[3] <= 0.013344999868422747) {
                            memcpy(var0, (double[]){0.1339205733054643, 0.8660794266945356}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.16176991150442477, 0.8382300884955752}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[0] <= 72.5) {
                            memcpy(var0, (double[]){0.18944099378881987, 0.8105590062111802}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.2607449856733524, 0.7392550143266475}, 2 * sizeof(double));
                        }
                    }
                } else {
                    if (input[3] <= 0.026554999873042107) {
                        if (input[2] <= 0.004504999844357371) {
                            memcpy(var0, (double[]){0.3783783783783784, 0.6216216216216216}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.219435736677116, 0.780564263322884}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[4] <= 0.004225000040605664) {
                            memcpy(var0, (double[]){0.6538461538461539, 0.34615384615384615}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.39285714285714285, 0.6071428571428571}, 2 * sizeof(double));
                        }
                    }
                }
            } else {
                if (input[3] <= 0.02284500002861023) {
                    if (input[4] <= 0.006615000078454614) {
                        if (input[1] <= 20.699999809265137) {
                            memcpy(var0, (double[]){0.24026512013256007, 0.7597348798674399}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.6666666666666666, 0.3333333333333333}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[0] <= 72.5) {
                            memcpy(var0, (double[]){0.31562167906482463, 0.6843783209351754}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.43014705882352944, 0.5698529411764706}, 2 * sizeof(double));
                        }
                    }
                } else {
                    if (input[4] <= 0.007065000012516975) {
                        if (input[4] <= 0.006435000104829669) {
                            memcpy(var0, (double[]){0.5555555555555556, 0.4444444444444444}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.3313953488372093, 0.6686046511627907}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[4] <= 0.007144999923184514) {
                            memcpy(var0, (double[]){0.8333333333333334, 0.16666666666666666}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.5437158469945356, 0.4562841530054645}, 2 * sizeof(double));
                        }
                    }
                }
            }
        }
    } else {
        if (input[3] <= 0.0036850000033155084) {
            if (input[3] <= 0.0011550000053830445) {
                if (input[3] <= 0.0004650000046240166) {
                    if (input[1] <= 61.10000038146973) {
                        if (input[0] <= 304.5) {
                            memcpy(var0, (double[]){0.15221040850587578, 0.8477895914941243}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.46153846153846156, 0.5384615384615384}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[0] <= 640.0) {
                            memcpy(var0, (double[]){0.11532846715328467, 0.8846715328467153}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){1.0, 0.0}, 2 * sizeof(double));
                        }
                    }
                } else {
                    if (input[1] <= 33.5) {
                        if (input[3] <= 0.0008249999955296516) {
                            memcpy(var0, (double[]){0.043478260869565216, 0.9565217391304348}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.17753623188405798, 0.822463768115942}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[4] <= 0.012054999824613333) {
                            memcpy(var0, (double[]){0.2019047619047619, 0.7980952380952381}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.2752, 0.7248}, 2 * sizeof(double));
                        }
                    }
                }
            } else {
                if (input[4] <= 0.013680000323802233) {
                    if (input[4] <= 0.009984999895095825) {
                        if (input[0] <= 116.5) {
                            memcpy(var0, (double[]){0.1683673469387755, 0.8316326530612245}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.3, 0.7}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[4] <= 0.010989999864250422) {
                            memcpy(var0, (double[]){0.38390092879256965, 0.6160990712074303}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.3032694475760992, 0.6967305524239008}, 2 * sizeof(double));
                        }
                    }
                } else {
                    if (input[1] <= 20.5) {
                        if (input[4] <= 0.01691999938338995) {
                            memcpy(var0, (double[]){0.288135593220339, 0.711864406779661}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.041666666666666664, 0.9583333333333334}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[3] <= 0.0023349999682977796) {
                            memcpy(var0, (double[]){0.34806629834254144, 0.6519337016574586}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.4449612403100775, 0.5550387596899224}, 2 * sizeof(double));
                        }
                    }
                }
            }
        } else {
            if (input[3] <= 0.010724999941885471) {
                if (input[4] <= 0.01335499994456768) {
                    if (input[1] <= 16.699999809265137) {
                        if (input[3] <= 0.00584500003606081) {
                            memcpy(var0, (double[]){0.271356783919598, 0.7286432160804021}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.3954022988505747, 0.6045977011494252}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[3] <= 0.006344999885186553) {
                            memcpy(var0, (double[]){0.4454225352112676, 0.5545774647887324}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.5424528301886793, 0.45754716981132076}, 2 * sizeof(double));
                        }
                    }
                } else {
                    if (input[3] <= 0.00723500014282763) {
                        if (input[0] <= 64.5) {
                            memcpy(var0, (double[]){0.3548387096774194, 0.6451612903225806}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.4770844837106571, 0.5229155162893429}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[4] <= 0.013995000161230564) {
                            memcpy(var0, (double[]){0.6073619631901841, 0.39263803680981596}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.5023668639053255, 0.4976331360946746}, 2 * sizeof(double));
                        }
                    }
                }
            } else {
                if (input[3] <= 0.020155000500380993) {
                    if (input[4] <= 0.014045000076293945) {
                        if (input[0] <= 31.5) {
                            memcpy(var0, (double[]){0.4284632853898562, 0.5715367146101439}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.538787023977433, 0.461212976022567}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[4] <= 0.022584999911487103) {
                            memcpy(var0, (double[]){0.5608989850169164, 0.4391010149830836}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.6151079136690647, 0.38489208633093525}, 2 * sizeof(double));
                        }
                    }
                } else {
                    if (input[2] <= 0.005455000093206763) {
                        if (input[2] <= 0.005095000145956874) {
                            memcpy(var0, (double[]){0.5844017094017094, 0.4155982905982906}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.25806451612903225, 0.7419354838709677}, 2 * sizeof(double));
                        }
                    } else {
                        if (input[3] <= 0.06351500004529953) {
                            memcpy(var0, (double[]){0.6085790884718498, 0.3914209115281501}, 2 * sizeof(double));
                        } else {
                            memcpy(var0, (double[]){0.6887755102040817, 0.3112244897959184}, 2 * sizeof(double));
                        }
                    }
                }
            }
        }
    }
    memcpy(output, var0, 2 * sizeof(double));
}

/* ---- end m2cgen-generated scoring function ---- */

/*
 * Friendly wrapper around the generated score().
 * Returns 1 if the model predicts "sleep", 0 if "keep polling".
 */
static inline int model_predicts_sleep(const double *feature_vector)
{
    double class_probabilities[2];
    score((double *)feature_vector, class_probabilities);
    return class_probabilities[1] > class_probabilities[0];
}

#endif /* MODEL_DECISION_H */
