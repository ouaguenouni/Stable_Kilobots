#include <math.h>
#include <stdint.h>   // for uint8_t and other types
#include <kilombo.h>

#include "kilotron_cuda.h"
#include "util.h"
#include "perceptron/matmul.h"

REGISTER_USERDATA(USERDATA)

#ifdef SIMULATOR



#else
#include <avr/io.h>  // for microcontroller register defs

//#define DEBUG          // for printf to serial port
//#include "debug.h"

#endif


#define COMM_R 85               // Communication range
#define DIFF_R 85               // Diffusion range
#define POLAR_TH 4.0            // Threshold to become polarized
#define EDGE_TH 0.8             // Ratio between the average number of neighbors of the robot and the average number of neighbors' neighbors for edge detection
#define WAIT_BEFORE_MOVE 27000  // kilo_ticks to wait before moving. 75000 for simulation, 27000 for real robots (about 10 minutes)
#define COUNTER_WAIT 8000       // kilo_ticks to wait when the robot tries to orbit but there is another robot orbiting in the area
#define DIST_CRIT 45            // Distance that a robot is considered to be close
#define R2 120                  // For probabilistic purposes

//Model_Parameter
#define A_VAL 0.08
#define B_VAL -0.08
#define C_VAL 0.03
#define D_VAL 0.03
#define E_VAL 0.1
#define F_VAL 0.12
#define G_VAL 0.06
#define D_u 0.5
#define D_v 10
#define LINEAR_R 160
#define SYNTH_U_MAX 0.23
#define SYNTH_V_MAX 0.5
#define DT 0.00005
//End_Parameters
/*
 * Message rx callback function. It pushes message to ring buffer.
 */
void rxbuffer_push(message_t *msg, distance_measurement_t *dist) {
    received_message_t *rmsg = &RB_back();
    rmsg->msg = *msg;
    rmsg->dist = *dist;
    RB_pushback();
}


/*
 * Transmission of the message
 */
message_t *message_tx()
{
  if (mydata->message_lock)
    return 0;
  return &mydata->transmit_msg;
}


/*
 * Changes the state of the robot
 */
void set_bot_state(int state){

	mydata->bot_state = state;

}


/*
 * Returns the state of the robot
 */
int get_bot_state(void){

	return mydata->bot_state;

}

/*
 * Returns whether the robot has any neighbor with the specified ID
 */
uint8_t has_neighbor_with_id(uint16_t id){

	uint8_t i;
	uint8_t flag = 0;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].ID == id){
			flag = 1;
			break;
		}

	}

	return flag;

}


/*
 * Returns whether the robot is polarized, i.e. concentration of molecule U is higher than POLAR_THRESHOLD
 */
uint8_t polarized(){

	uint8_t flag = 0;

	if(mydata->molecules_concentration[0] > POLAR_TH){
		flag = 1;
	}

	return flag;

}


/*
 * Returns the distance to the nearest polarized neighbor
 */
uint8_t get_dist_to_nearest_polarized(){

	uint8_t i;
	uint8_t dist = 255;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].molecules_concentration[0] > POLAR_TH){

			if(mydata->neighbors[i].dist  < dist) dist = mydata->neighbors[i].dist;

		}

	}

	return dist;

}


/*
 * Returns the id of the nearest neighbor with more than one neighbor
 */
uint16_t find_nearest_N_id(){

	uint8_t i;
	uint16_t id = 0;
	uint8_t dist = 255;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].dist  < dist && mydata->neighbors[i].N_Neighbors > 1) {
			id = mydata->neighbors[i].ID;
			dist = mydata->neighbors[i].dist;
		}
	}

	return id;
}


/*
 * Returns the distance to the furthest polarized neighbor
 */
uint16_t find_most_distant_N_id(){

	uint8_t i;
	uint16_t id = 0;
	uint8_t dist = 0;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].dist  > dist) {
			id = i;
			dist = mydata->neighbors[i].dist;
		}

	}

	return id;
}


/*
 * Returns the distance of the neighbor with the specified ID
 */
uint8_t get_dist_by_id(uint16_t id){

	uint8_t i;
	uint8_t dist = 255;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].ID == id){
			dist = mydata->neighbors[i].dist;
			break;
		}

	}

	return dist;

}


/*
 * Returns the state of the neighbor with the specified ID
 */
uint8_t get_state_by_id(uint16_t id){

	uint8_t i;
	uint8_t state = WAIT;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].ID == id){
			state = mydata->neighbors[i].n_bot_state;
			break;
		}

	}

	return state;

}



/*
 * Returns the difference in distance with respect to the last update of the neighbor with the specified ID
 */
int get_diff_dist_by_id(uint16_t id){

	uint8_t i;
	int diff_dist = 255;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].ID == id){
			diff_dist = mydata->neighbors[i].delta_dist;
			break;
		}

	}

	return diff_dist;

}



/*
 *  Returns true if all neighbors are in WAIT state
 */
uint8_t check_wait_state(){

	uint8_t i;
	uint8_t flag = 1;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].n_bot_state != WAIT) {
			flag = 0;
			break;
		}

	}

	return flag;

}


/*
 * Computes the average number of neighbors' neighbors taking into account the distance to them
 */
float calc_avg_NNs(){

	float sum = 0;
	float w_sum = 0;
	uint8_t i;

	for(i = 0; i < mydata->N_Neighbors; i++){

			float w = 1/(float) mydata->neighbors[i].dist;
			sum = sum + w * (float) mydata->neighbors[i].N_Neighbors;
			w_sum = w_sum + w;

	}

	return sum/w_sum;

}


/*
 * Function to compute a running average by weighing previous and current observations
 */
float calc_apprx_running_avg(float old_avg, float num, float alpha){

	float avg;

	avg = alpha * num + (1 - alpha) * old_avg;

	return avg;

}


/*
 * Concentration of U and V is updated based on the linear model for reaction-diffusion
 */
void regulation_linear_model(){

    // D_u and D_v
	float D[2];
	D[0] = D_u;
	D[1] = D_v;

    // R
	float linear_R = LINEAR_R;

	// Laplace operator for U and V
	float lap[2];
	lap[0] = 0;
	lap[1] = 0;

    // The Laplace operator is calculated for U and V
    int i;
	for (i = 0; i < mydata->N_Neighbors; i++)
	{

	  float weight;

        // All neighbors contribute the same
        weight = 1.0;

        // Only neighbors nor further apart than DIFF_R (currently 85 mm) and not moving are counted for diffusion
        if(mydata->neighbors[i].dist <= DIFF_R && mydata->neighbors[i].n_bot_state != ORBIT && mydata->neighbors[i].n_bot_state != FOLLOW){
          lap[0] += weight * (mydata->neighbors[i].molecules_concentration[0] - mydata->molecules_concentration[0]);
          lap[1] += weight * (mydata->neighbors[i].molecules_concentration[1] - mydata->molecules_concentration[1]);
        }
	}


	float synth_u_max = SYNTH_U_MAX;
	float synth_v_max = SYNTH_V_MAX;

    // These variables will have f(u,v) and g(u,v) eventually
	float synth_rate_u;
	float synth_rate_v;

	synth_rate_u = (A_VAL * mydata->molecules_concentration[0] + B_VAL * mydata->molecules_concentration[1] + C_VAL);
	synth_rate_v = (E_VAL * mydata->molecules_concentration[0] - F_VAL);

	if(synth_rate_u < 0) synth_rate_u = 0;
	if(synth_rate_u >= synth_u_max) synth_rate_u = synth_u_max;

	if(synth_rate_v < 0) synth_rate_v = 0;
	if(synth_rate_v >= synth_v_max) synth_rate_v = synth_v_max;

	synth_rate_u = synth_rate_u - D_VAL * mydata->molecules_concentration[0];
	synth_rate_v = synth_rate_v - G_VAL * mydata->molecules_concentration[1];

    // Rate of change in the concentration
	float dG[2];
	dG[0] = linear_R * synth_rate_u + D[0] * lap[0];
	dG[1] = linear_R * synth_rate_v + D[1] * lap[1];

	// Update of the concentration
	float dt = DT;
	mydata->molecules_concentration[0] += dt * dG[0];
	mydata->molecules_concentration[1] += dt * dG[1];
}

void prediction_color() {

    //printf("%f\n", mydata->prediction);
    set_color(RGB((int)(255*(mydata->prediction1)),0,(int)(255*(mydata->prediction2))));

}

/*
 * Returns whether the robot is on the edge based on the ratio of its neighbors and its neighbors' neighbors
 */
uint8_t edge_prob_running_avg_ratio_NNs(){

	uint8_t  prob;
	float diff_th = EDGE_TH;

	if(mydata->running_avg_Ns/mydata->running_avg_NNs < diff_th) prob = 1;
	else prob = 0;

	return prob;

}


/*
 * Returns whether the robot is on the edge based on the ratio of its neighbors and its neighbors' neighbors
 */
uint8_t test_edge(){

	return edge_prob_running_avg_ratio_NNs();

}

/*
 * A random byte is generated
 */
uint8_t rand_byte(){

	return rand_soft();

}


/*
 * A unique, local ID is found for the robot
 */
uint16_t reset_id(){

	uint16_t id;
	id = (rand_byte() << 8) | rand_byte();

	return id;

}


/*
 * Returns whether the robot has at least the specified number of polarized neighbors
 */
uint8_t has_at_least_n_polarized_N(uint8_t n){

	uint8_t i;
	uint8_t count = 0;
	uint8_t flag = 0;

	for(i = 0; i < mydata->N_Neighbors; i++){

		if(mydata->neighbors[i].molecules_concentration[0] > POLAR_TH){
			count++;

		}

		if(count == n){
			flag = 1;
			break;
		}

	}

	return flag;

}

//PERCEPTRON
void process_perceptron(){

    uint8_t i,j = 0;

    float *x = (float*)malloc(2+COMMUNICATION*sizeof(float));
    x[0]=mydata->molecules_concentration[0];
    x[1]=mydata->molecules_concentration[1];

    for (i = 0;i<mydata->N_Neighbors; i++) {

      for(j=0;j<COMMUNICATION;j++){
        x[j+2]=x[j+2]+mydata->neighbors[i].communication_chanel[j];
      }

    }

    float *prediction = predict(mydata->perceptron, x);
    //printf("Prédiction 01 : %f \n",prediction[0]);
    //printf("Prédiction 02 : %f \n",prediction[1]);
    mydata->prediction1 = prediction[0];
    mydata->prediction2 = prediction[1];
    //printf("%f\n", mydata->prediction);
    for (i = 0;i<COMMUNICATION;i++){
      mydata->communication_chanel[i] = prediction[i+2];
    }

    free(x);

}

/*
 * It processes a received message at the front of the ring buffer.
 * It goes through the list of neighbors. If the message is from a bot
 * already in the list, it updates the information, otherwise
 * it adds a new entry to the list
 */
void process_message()
{
  uint8_t i,j;
  uint16_t ID;

  uint8_t *data = RB_front().msg.data;


  ID = data[0] | (data[1] << 8);
  uint8_t d = estimate_distance(&RB_front().dist);


  if(d > COMM_R && mydata->N_Neighbors > 0 && get_bot_state() != FOLLOW) return;

  // search the neighbor list by ID
  for (i = 0; i < mydata->N_Neighbors; i++)
      if (mydata->neighbors[i].ID == ID) { // found it
          mydata->neighbors[i].delta_dist = d - mydata->neighbors[i].dist;
          break;
      }

      if (i == mydata->N_Neighbors) {  // this neighbor is not in list
          if (mydata->N_Neighbors < MAXN - 1) // neighbor list is not full
              mydata->N_Neighbors++;
          else
              i = find_most_distant_N_id(); // overwrite the most distant neighbor

          mydata->neighbors[i].delta_dist = 0;

      }

      // i now points to where this message should be stored
      mydata->neighbors[i].ID = ID;
      mydata->neighbors[i].timestamp = kilo_ticks;
      mydata->neighbors[i].dist = d;
      mydata->neighbors[i].N_Neighbors = data[2];
      mydata->neighbors[i].n_bot_state = data[7];

      int c;

      for (c=0;c<COMMUNICATION;c++){
        mydata->neighbors[i].communication_chanel[c] = data[8+c];
      }

      uint8_t signo_rec;
      uint8_t exp_rec;
      uint16_t mant_rec;
      uint16_t bit1;
      float mant_fl;

      signo_rec = 0;
      exp_rec = 0;
      mant_rec = 0;
      bit1 = 0;


      int jj;


      for (j = 0; j < 2; j++) {

          // recover from "half" precision
          signo_rec = data[3 + j] >> 7;
          exp_rec = (data[3 + j] >> 2) & 0x1F;
          mant_rec = ((data[3 + j] & 0x3) << 8) | data[3 + 2 + j];

          mant_fl = 0;
          for (jj = 9; jj >= 0; jj--) {

              bit1 = mant_rec >> jj;

              mant_fl = mant_fl + bit1 * pow(2, jj - 10);

              mant_rec = mant_rec - bit1 * pow(2, jj);

          }


          if (exp_rec == 31 && signo_rec == 0) mydata->neighbors[i].molecules_concentration[j] = 65504;

          else if (exp_rec == 31 && signo_rec == 1) mydata->neighbors[i].molecules_concentration[j] = -65504;

          else if (exp_rec == -15 && mant_rec == 0) mydata->neighbors[i].molecules_concentration[j] = 0;

          else if (exp_rec == -15 && mant_rec != 0)
              mydata->neighbors[i].molecules_concentration[j] = (float) pow(-1, signo_rec) * pow(2, exp_rec - 15 + 1) *
                                                                (0 + mant_fl);

          else
              mydata->neighbors[i].molecules_concentration[j] =
                      (float) pow(-1, signo_rec) * pow(2, exp_rec - 15) * (1 + mant_fl);

      }

}

/*
 * This function:
 *   - Processes all messages received since the last time
 *   - Updates neighbors table
 *   - Updates Number of neighbors and Number of neighbors' neighbors running averages
 */
void receive_inputs()
{

    // Processes al messages received since the last time the bot read them (removed after reading)
    while (!RB_empty()) {
        process_message();
        RB_popfront();
    }

    float alpha = 0.0001;

    mydata->running_avg_Ns = calc_apprx_running_avg(mydata->running_avg_Ns, mydata->N_Neighbors, alpha);

    if(mydata->N_Neighbors > 0){
      mydata->running_avg_NNs = calc_apprx_running_avg(mydata->running_avg_NNs, calc_avg_NNs(), alpha);
    }
    else mydata->running_avg_NNs = calc_apprx_running_avg(mydata->running_avg_NNs, 0, alpha);

}


/*
 * Goes through the list of neighbors and removes entries older than a threshold, currently 2 seconds.
 */
void purgeNeighbors(void)
{
  int8_t i;

  for (i = mydata->N_Neighbors-1; i >= 0; i--)
      if (kilo_ticks - mydata->neighbors[i].timestamp  > 64)
      { //this one is too old.
        mydata->neighbors[i] = mydata->neighbors[mydata->N_Neighbors-1]; //replace it by the last entry
        mydata->N_Neighbors--;
      }
}


/*
 * The message is updated to reflect changes in state, concentration, number of neighbors and ID
 */
void setup_message(void)
{
  mydata->message_lock = 1;  // don't transmit while we are forming the message
  mydata->transmit_msg.type = NORMAL;
  mydata->transmit_msg.data[0] = kilo_uid & 0xff; //0: low  ID
  mydata->transmit_msg.data[1] = kilo_uid >> 8;   //1: high ID

  mydata->transmit_msg.data[2] = mydata->N_Neighbors; //2: number of neighbors
  mydata->transmit_msg.data[7] = get_bot_state(); // 7: state of the robot

  int c;

  for (c=0;c<COMMUNICATION;c++){
    mydata->transmit_msg.data[8+c] = mydata->communication_chanel[c];
  }

  int i;

  uint8_t signo;
  uint8_t exp;
  uint32_t mant;
  uint8_t exp_h;
  uint16_t mant_h;
  uint8_t byte_1;
  uint8_t byte_2;
  int exp_real;
  long fl;


  for (i = 0; i < 2; i++){

	  fl = *(long*)&mydata->molecules_concentration[i];
	  signo = fl >> 31;
	  exp = (fl >>23) & 0xff;
	  mant =  fl & 0x7FFFFF;

	  // Convert to "half" precision

	  exp_real = exp-127;

	  //cut the size of the exponent
	  if(exp_real<-15) exp_real=-15;
	  else if(exp_real>16) exp_real=16;

	  exp_h= exp_real + 15;
	  mant_h = mant >> 13;

	  byte_1 = signo << 7 | exp_h << 2 | mant_h >> 8;
	  byte_2 = mant_h & 0xff;

	  mydata->transmit_msg.data[3+i] = byte_1;
	  mydata->transmit_msg.data[3+2+i] = byte_2;

  }

  mydata->transmit_msg.crc = message_crc(&mydata->transmit_msg);
  mydata->message_lock = 0;
}
/*
 * In this function:
 *   - Random initialisation of concentration of molecules U and V
 *   - Random ID
 *   - Variables are initialised to default/empty values
 *       - Bot is stopped, in WAIT mode and set to clockwise movement
 *   - The message starts to be sent, with the default/empty variables
 */
void setup() {

    // Initialization of the random generator
    while(get_voltage() == -1);
    rand_seed(rand_hard());

    float a0_b;
    float a1_b;

    // Random percentage of concentration of molecules U and V
    a0_b=rand_byte()*100/255;
    a1_b=rand_byte()*100/255;

    // Number of molecules U and V, from 0 to 6 (continuous values)
    mydata->molecules_concentration[0] = (float) a0_b*0.01*6;
    mydata->molecules_concentration[1] = (float) a1_b*0.01*6;

    // Random ID
    kilo_uid = rand_byte();

    set_bot_state(WAIT);

    // Lock unblocked
    mydata->message_lock = 0;

    // The robot will start orbiting in the opposite direction, i.e. right (clockwise)
    mydata->last_turn = LEFT;

    // Not moving
    mydata->move_switch_flag = 0;

    // Initialisation
    mydata->N_Neighbors = 0;

    // Allowed to orbit the first time
    mydata->counter = 0;

    // Initialization
    mydata->running_avg_NNs = 0;
    mydata->running_avg_Ns = 0;

    //PERCEPTRON
    mydata->perceptron = new_perceptron_config("perceptron/NN.txt");
    load_weights(mydata->perceptron,"weights.txt");

    mydata->communication_chanel=(float*)malloc(COMMUNICATION * sizeof(float));
    int i;
    for (i = 0;i<COMMUNICATION;i++){
      mydata->communication_chanel[i] = 1;
    }

    // The message is initialized
    setup_message();
}


/*
 * Loop function that the kilobots execute continuously.
 *   - It processes messages, and updates neighbors tables and N, NN running averages
 *   - If robot not in state ORBIT or FOLLOW and has neighbors, run Turing patterning
 */
void loop(){

    // Processes messages, and updates neighbors tables and N, NN running averages
	receive_inputs();

    regulation_linear_model();

    process_perceptron();

    prediction_color();
    // Allows some time to start with the running averages
	if(kilo_ticks == 250){
		mydata->running_avg_Ns = mydata->N_Neighbors;
		mydata->running_avg_NNs = calc_avg_NNs();
	}

    // Unique, local ID
	if(has_neighbor_with_id(kilo_uid)) {
		kilo_uid = reset_id();
		set_color(RGB(3,3,0));
	}

    // Old entries in the neighbors' table are removed
	purgeNeighbors();

    // Message is updated
	setup_message();

}

void kilofree(){

    free_perceptron(mydata->perceptron);

}



extern char* (*callback_botinfo) (void);
int16_t circle_barrier(double x, double y, double * dx, double * dy);
char *botinfo(void);

//#ifdef SIMULATOR

#include <jansson.h>
json_t *json_state();

//#endif

int main(void)
{
  kilo_init();

#ifdef DEBUG
  debug_init();
#endif

#ifdef SIMULATOR
  SET_CALLBACK(botinfo, botinfo);
  SET_CALLBACK(reset, setup);

#endif

#ifndef KILOBOT
  callback_botinfo = botinfo;
#endif

  //initialize ring buffer
  RB_init();
  kilo_message_rx = rxbuffer_push;
  kilo_message_tx = message_tx;   // register our transmission function

  kilo_start(setup, loop);

#ifdef SIMULATOR
  SET_CALLBACK(json_state, json_state);
#endif
  return 0;
}




#ifndef KILOBOT
static char botinfo_buffer[10000];

// provides a text string for the status bar, about this bot
char *botinfo(void)
{
	int n;
	  char *p = botinfo_buffer;
	  n = sprintf (p, "ID: %d  ", kilo_uid);
	  p += n;


	 //SPOTS

	   n = sprintf (p, "G(green): %5.3f  ", mydata->molecules_concentration[0]);
	   p += n;
	   n = sprintf (p, "G(red): %5.3f  ", mydata->molecules_concentration[1]);
	   p += n;

	   n = sprintf (p, "sum: %5.3f  ", mydata->molecules_concentration[0] + mydata->molecules_concentration[1]);
	   p += n;


	   n = sprintf (p, "N: %i ", mydata->N_Neighbors);
	   p += n;

	   return botinfo_buffer;
}
#endif
