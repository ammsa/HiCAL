#include <iostream>
#include <mutex>
#include <thread>
#include <algorithm>
#include "bmi.h"

using namespace std;
BMI::BMI(const SfSparseVector &seed,
        Scorer *_scorer,
        int _num_threads,
        int _judgments_per_iteration,
        int _max_effort,
        int _max_iterations,
        bool _async_mode)
    :scorer(_scorer),
    num_threads(_num_threads),
    judgments_per_iteration(_judgments_per_iteration),
    max_effort(_max_effort),
    max_iterations(_max_iterations),
    async_mode(_async_mode),
    training_data(get_initial_training_data(seed))
{
    is_bmi = (judgments_per_iteration == -1);
    if(is_bmi || _async_mode)
        judgments_per_iteration = 1;
    perform_iteration();
}

void BMI::finish_session(){
    add_to_judgment_list({-1});
    state.finished = true;
}

void BMI::perform_iteration(){
    if(max_iterations != -1 && state.cur_iteration >= max_iterations){
        finish_session();
    }

    if(!state.finished){
        lock_guard<mutex> lock(state_mutex);
        cerr<<"Beginning Iteration "<<state.cur_iteration<<endl;
        auto results = perform_training_iteration();
        cerr<<"Fetched "<<results.size()<<" documents"<<endl;
        add_to_judgment_list(results);
        state.next_iteration_target += judgments_per_iteration;
        if(is_bmi)
            judgments_per_iteration += (judgments_per_iteration + 9)/10;
        state.cur_iteration++;
    }
}

void BMI::perform_iteration_async(){
    if(async_training_mutex.try_lock()){
        while(training_cache.size() > 0){
            if(max_iterations != -1 && state.cur_iteration >= max_iterations){
                finish_session();
                return;
            }

            if(!state.finished){
                lock_guard<mutex> lock(state_mutex);
                cerr<<"Beginning Iteration "<<state.cur_iteration<<endl;
                auto results = perform_training_iteration();
                cerr<<"Fetched "<<results.size()<<" documents"<<endl;
                add_to_judgment_list(results);
                state.cur_iteration++;
            }
        }
        async_training_mutex.unlock();
    }
}

void BMI::randomize_non_rel_docs(){
    uniform_int_distribution<int> distribution(0, scorer->doc_features.size()-1);
    for(int i = 1;i<=100;i++){
        int idx = distribution(rand_generator);
        if(training_data.NumExamples() < i+1)
            training_data.AddLabeledVector(scorer->doc_features[idx], -1);
        else
            training_data.ModifyLabeledVector(i, scorer->doc_features[idx], -1);
    }
}

SfDataSet BMI::get_initial_training_data(const SfSparseVector &seed){
    // Todo generalize seed docs (support multiple rel/non-rel seeds)
    SfDataSet training_data = SfDataSet(true);
    training_data.AddLabeledVector(seed, 1);
    return training_data;
}

void BMI::train(SfWeightVector &w){
    sofia_ml::StochasticRocLoop(training_data,
            sofia_ml::LOGREG_PEGASOS,
            sofia_ml::PEGASOS_ETA,
            0.0001,
            10000000.0,
            200000,
            &w);
}

vector<string> BMI::get_doc_to_judge(int count=1){
    while(true){
        {
            lock_guard<mutex> lock(judgment_list_mutex);
            lock_guard<mutex> lock2(training_cache_mutex);
            if(judgment_list.size() > 0){
                vector<string> ret;
                for(int id: judgment_list){
                    if(id == -1 || ret.size() >= count)
                        break;
                    ret.push_back(scorer->doc_features[id].doc_id);
                }
                return ret;
            }
            /* for(int id: judgment_list){ */
            /*     // poison value */
            /*     if(id == -1){ */
            /*         return {}; */
            /*     } */
            /*     if(finished_judgments.find(id) == finished_judgments.end() \ */
            /*             && training_cache.find(id) == training_cache.end()) */
            /*         return doc_features[id].doc_id; */
            /* } */
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void BMI::add_to_judgment_list(const vector<int> &ids){
    lock_guard<mutex> lock(judgment_list_mutex);
    judgment_list = ids;
}

void BMI::wait_for_judgments(){
    while(1){
        {
            // NOTE: avoid deadlock by making sure whichever thread locks these two mutexes,
            // they always do in the same order
            lock_guard<mutex> lock(judgment_list_mutex);
            if(judgment_list.size() == 0)
                return;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void BMI::add_to_training_cache(int id, int judgment){
    lock_guard<mutex> lock(training_cache_mutex);
    training_cache[id] = judgment;
}

void BMI::remove_from_judgment_list(int id){
    lock_guard<mutex> lock(judgment_list_mutex);
    auto it = std::find(judgment_list.begin(), judgment_list.end(), id);
    if(it != judgment_list.end())
        judgment_list.erase(it);
}

void BMI::record_judgment(string doc_id, int judgment){
    int id = scorer->doc_ids_inv_map[doc_id];
    add_to_training_cache(id, judgment);
    remove_from_judgment_list(id);

    if(!async_mode){
        if(finished_judgments.size() + training_cache.size() >= state.next_iteration_target)
            perform_iteration();
    }else{
        auto t = thread(&BMI::perform_iteration_async, this);
        t.detach();
    }
}

vector<int> BMI::perform_training_iteration(){
    lock_guard<mutex> lock_training(training_mutex);
    randomize_non_rel_docs();

    {
        lock_guard<mutex> lock(training_cache_mutex);
        for(pair<int, int> training: training_cache){
            training_data.AddLabeledVector(scorer->doc_features[training.first], training.second);
            finished_judgments.insert(training.first);
        }
        training_cache.clear();
    }

    // Training
    auto start = std::chrono::steady_clock::now();

    SfWeightVector w(scorer->dimensionality);
    train(w);

    auto weights = w.AsFloatVector();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds> 
        (std::chrono::steady_clock::now() - start);
    cerr<<"Training finished in "<<duration.count()<<"ms"<<endl;

    vector<int> results;

    // Scoring
    start = std::chrono::steady_clock::now();
    scorer->rescore_documents(weights, num_threads, judgments_per_iteration+extra_judgment_docs, finished_judgments, results);
    duration = std::chrono::duration_cast<std::chrono::milliseconds> 
        (std::chrono::steady_clock::now() - start);
    cerr<<"Rescored "<<scorer->doc_features.size()<<" documents in "<<duration.count()<<"ms"<<endl;

    state.weights = weights;

    return results;
}

void BMI::run()
{
    is_bmi = (judgments_per_iteration == -1);
    if(is_bmi)
        judgments_per_iteration = 1;

    for(int cur_iteration = 0;;cur_iteration++){
        if(max_iterations != -1 && cur_iteration >= max_iterations){
            add_to_judgment_list({-1});
        }

        {
            lock_guard<mutex> lock(judgment_list_mutex);
            if(judgment_list.size() > 0 && judgment_list[0] == -1)
                return;
        }

        cerr<<"Beginning Iteration "<<cur_iteration<<endl;
        auto results = perform_training_iteration();
        add_to_judgment_list(results);
        wait_for_judgments();

        if(is_bmi){
            judgments_per_iteration += (judgments_per_iteration + 9)/10;
        }
    }
}

