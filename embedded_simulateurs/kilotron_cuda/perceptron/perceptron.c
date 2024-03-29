//
// Created by steag on 29/02/2020.
//
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <cuda_runtime.h>
#include <cuda.h>

#include "perceptron.h"
#include "matmul.h"
#include "NeuralNetwork.h"

Perceptron *new_perceptron(int shape[],int nb_layers){

    Perceptron *perc = (Perceptron*)malloc(sizeof(Perceptron));
    perc->nb_input=shape[0];
    perc->nb_layers=nb_layers-1;
    perc->layers=(Layer**)malloc((nb_layers-1)* sizeof(Layer*));
    int i;
    for (i=0;i<nb_layers-1;i++){
        perc->layers[i]=new_layer(shape[i],shape[i+1]);
        assert( perc->layers[i] != NULL );
        //display_layer(perc->layers[i]);
    }

    return perc;

}

int *read_config (const char* file_name)
{
  FILE* file = fopen (file_name, "r");
  int *config = (int*)malloc(5*sizeof(int));
  int i,d;

  for(i = 0;i<5;i++)
  {
    if(!feof (file)){
      fscanf (file, "%d\n", &d);
      //printf ("%d\n", d);
      config[i]=d;
    }
  }
  fclose (file);
  return config;
}

Perceptron *new_perceptron_config(const char* file_name){

    int *config = read_config(file_name);

    int nb_layers = 2+config[3];

    int *shape = (int*)malloc(nb_layers*sizeof(int));
    int i;

    shape[0] = config[0] + config[2];
    shape[nb_layers-1] = config[1] + config[2];

    for(int i = 1;i<nb_layers-1;i++){
      shape[i] = config[4];
    }

    free(config);

    Perceptron *perc = (Perceptron*)malloc(sizeof(Perceptron));

    perc->nb_input=shape[0];
    perc->nb_layers=nb_layers-1;
    perc->layers=(Layer**)malloc((nb_layers-1)* sizeof(Layer*));
    for (i=0;i<nb_layers-1;i++){
        perc->layers[i]=new_layer(shape[i],shape[i+1]);
        assert( perc->layers[i] != NULL );
        //display_layer(perc->layers[i]);
    }

    free(shape);

    return perc;
}

float *predict(Perceptron *perc,float *input){

    int i;
    float *a = input;
    for (i=0;i<perc->nb_layers;i++) {
        a = compute_layer(perc->layers[i], a);
    }

    //printf("prediction : ");
    //displayMat(a,6,1);

    return a;

}

void set_layer(Perceptron *perc,int layer_nb,float **weights,float **bias){

    perc->layers[layer_nb]->weights= weights;
    perc->layers[layer_nb]->bias=bias;

}

void read_weights (const char* file_name)
{
  FILE* file = fopen (file_name, "r");
  float i = 0;

  fscanf (file, "%f\n", &i);
  while (!feof (file))
    {
      printf ("%f\n", i);
      fscanf (file, "%f\n", &i);
    }
  fclose (file);
}

void write_weights(Perceptron *perc,char* file_name){
    FILE* file = fopen (file_name, "w");

    int l,i,j;

    for(l=0;l<perc->nb_layers;l++){


        for(i=0;i<perc->layers[l]->nb_neurons;i++){
          for(j=0;j<perc->layers[l]->nb_input;j++){

              fprintf(file, "%f\n",perc->layers[l]->weights[i][j]);

          }
        }

        for(i=0;i<perc->layers[l]->nb_neurons;i++){
            fprintf(file, "%f\n",perc->layers[l]->bias[i][0]);
        }

    }

    fclose (file);

}

void load_weights(Perceptron *perc,char* file_name){
    FILE* file = fopen (file_name, "r");

    int l,i,j,c=0;
    float w = 0;

    //printf("LOADING");

    for(l=0;l<perc->nb_layers;l++){

        for(i=0;i<perc->layers[l]->nb_neurons;i++){
          for(j=0;j<perc->layers[l]->nb_input;j++){

              fscanf (file, "%f", &w);
              perc->layers[l]->weights[i][j] = w;

          }
        }

        for(i=0;i<perc->layers[l]->nb_neurons;i++){

            fscanf (file, "%f", &w);
            perc->layers[l]->bias[i][0] = w;

        }

    }

    fclose (file);

}

void free_perceptron(Perceptron *perc){



  int i;
  for (i=0;i<perc->nb_layers;i++){
      free_layer(perc->layers[i]);
  }

  free(perc);

}
