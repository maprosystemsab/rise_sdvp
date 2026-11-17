#include <setjmp.h>
#include <math.h>
#include "generator.c"
systime_t sim_now=1; int main_id=0; bool sim_timeout=false;
static jmp_buf jb; static int steg_kvar;
// Honda + Goldenmotor-modell
static float rpm=0, cmd_crank=0, cmd_brake=0; static bool kill_open=false, kan_tanda=true, can_tyst=false;
static float tand_tid=0; static bool gar=false; static uint32_t cmd_tid=0;
static can_status_msg st={60,0,0,0,0,51.0f};
can_status_msg *comm_can_get_status_msg_id(int id){ if(id!=60) return 0; return &st; }
void comm_can_transmit_eid(uint32_t id,uint8_t*d,uint8_t l){ (void)l; int32_t i=0; float a=buffer_get_int32(d,&i)/1000.0f;
  int cmd=id>>8; cmd_tid=sim_now; if(cmd==1){cmd_crank=a;cmd_brake=0;} else {cmd_brake=a;cmd_crank=0;} }
void comm_can_transmit_sid(uint32_t id,uint8_t*d,uint8_t l){ (void)id;(void)l; kill_open=d[1]==1; }
static void fysik(float dt){
  if(sim_now-cmd_tid>1000){cmd_crank=cmd_brake=0;} // VESC-timeout
  if(!kill_open) gar=false;
  if(!gar && kill_open && kan_tanda && rpm>300){ tand_tid+=dt; if(tand_tid>0.4f) gar=true; } else if(!gar) tand_tid=0;
  float mal;
  if(gar) mal=3000.0f - cmd_brake*8.0f; // regulator, last drar ned
  else mal=cmd_crank*10.0f;             // vevvarv ~ 10 rpm/A
  if(gar && mal<1400) mal=1400;
  float k = gar?2.0f:3.0f; rpm += (mal-rpm)*k*dt; if(rpm<0)rpm=0;
  if(!can_tyst){ st.rx_time=sim_now; st.rpm=rpm*4.0f; st.current= gar? -cmd_brake : cmd_crank; }
}
void chThdSleepMilliseconds(int ms){ fysik(ms/1000.0f); sim_now+=ms; if(--steg_kvar<=0) longjmp(jb,1); }
static void kor(float sek){ steg_kvar=(int)(sek*50); if(!setjmp(jb)) gen_thread(0); }
static void rapport(const char*t){ printf("  => %s: lage=%s fel=%s rpm=%.0f kill_oppen=%d gar=%d\n",t,lage_namn(m_lage),fel_namn(m_fel),rpm,kill_open,gar); }
static void nollstall(void){ m_lage=GEN_AV; m_fel=GEN_FEL_INGET; rpm=0; gar=false; kan_tanda=true; can_tyst=false; m_auto=false; sim_timeout=false; m_vill_start=m_vill_stopp=false; kor(0.5);}
int main(void){
  int fel=0;
  printf("TEST 1: normal start -> laddar -> stopp\n"); nollstall();
  generator_start(); kor(8); rapport("efter 8 s"); if(m_lage!=GEN_LADDAR||!gar) fel++;
  printf("    drivfaktor under laddning = %.2f\n", generator_effekt_faktor()); if(generator_effekt_faktor()!=0.5f) fel++;
  generator_stopp(); kor(6); rapport("efter stopp"); if(m_lage!=GEN_AV||gar||kill_open) fel++;
  printf("TEST 2: Hondan tänder inte\n"); nollstall(); kan_tanda=false;
  generator_start(); kor(25); rapport("efter 25 s"); if(m_lage!=GEN_STOPPAD_FEL||m_fel!=GEN_FEL_STARTADE_INTE||kill_open) fel++;
  generator_stopp(); kor(0.5); rapport("kvitterat"); if(m_lage!=GEN_AV) fel++;
  printf("TEST 3: CAN tystnar under laddning\n"); nollstall(); generator_start(); kor(8); can_tyst=true; kor(1.5); rapport("CAN tyst"); if(m_fel!=GEN_FEL_CAN||kill_open) fel++;
  kor(4); rapport("4 s senare"); if(gar) fel++;
  printf("TEST 4: nödstopp mitt i vevningen\n"); nollstall(); generator_start(); kor(0.6); generator_nodstopp(); rapport("direkt efter nödstopp (kabel)"); if(kill_open) fel++; kor(0.1); rapport("efter ett varv"); if(kill_open||m_fel!=GEN_FEL_NODSTOPP) fel++;
  kor(3); rapport("3 s senare"); if(gar||m_lage!=GEN_STOPPAD_FEL) fel++;
  printf("TEST 5: Maxim+ hörs inte vid start\n"); nollstall(); can_tyst=true; st.rx_time=0; sim_now+=5000; generator_start(); kor(1); rapport("start utan CAN"); if(m_fel!=GEN_FEL_CAN||kill_open) fel++;
  printf("TEST 6: batteri fullt stoppar\n"); nollstall(); m_full_v=55; generator_start(); kor(8); st.v_in=56; kor(6); rapport("56 V"); if(m_lage!=GEN_AV||gar) fel++; m_full_v=0; st.v_in=51;
  printf("TEST 7: ingen kontakt med föraren (tapp 2 s)\n"); nollstall(); m_tapp_s=2; generator_start(); kor(8); sim_timeout=true; kor(9); rapport("tappad länk"); if(gar||m_fel!=GEN_FEL_INGEN_KONTAKT) fel++; m_tapp_s=120;
  printf("TEST 8: automatik startar under 48 V\n"); nollstall(); m_start_v=48; st.v_in=47; generator_auto(true); kor(8); rapport("auto"); if(m_lage!=GEN_LADDAR) fel++; m_start_v=0; st.v_in=51;
  printf("\n%s (%d fel)\n", fel?"MISSLYCKADES":"ALLA TESTER OK", fel); return fel;
}
