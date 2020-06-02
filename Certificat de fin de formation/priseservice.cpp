/*****************************************************************************
*
* Nom du fichier : priseservice.cpp
*
******************************************************************************
*
* Auteur : Sylvain GIRARD (ASTEK)                Date de creation : 11/06/2012
*
******************************************************************************
* Description du fichier
*
* Identification Agent en mode nominal et d�grad�
*
******************************************************************************
* Historique des revisions :
*
* version      date            auteur
*
* 00           11/06/2012      Sylvain GIRARD (ASTEK)
*
*
* 01           21/11/2012      Olivier MOLANCHON
*
* - Arrete la temporisation d'effraction � l'init qui avait �t� arm�e suite � l'ouverture porte
*
* 02            29/11/2012	    Olivier MOLANCHON
*
* - Vente conducteur
*
* 03            06/05/2012        Olivier MOLANCHON
*
* - Ajout du code retour PAR_AGENT_INIT pour indiquer qu'il n'y a pas d'agent / pas de param�tre
*   et ainsi proposer le menu agent minimum permettant l'initialisation sans code pin
*
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <util.h>
#include <apitlb.h>
#include <gt.h>
#include <es.h>
#include <hanover.h>
#include <rhm.h>
#include <parics.h>
#include <ics.h>
#include <par.h>
#include <bdd.h>
#include <etat.h>
#include <agent.h>
#include <agents.h>
#include <lstfct.h>
#include <hs.h>
#include <finint.h>
#include <priseservice.h>
#include <configDat.h>
#include <vente.h>
#include <CscConsultation.h>

/*****************************************************************************
* But de la fonction : Constructeur
* --------------------
* Parametres :
* ------------
* entree : Prise de service d�grad�e true / false
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
T_EtatPriseDeService::T_EtatPriseDeService(T_ParLangue Langue_P, unsigned int Degradee_P) : T_Etat()

{
   TraceEtat.Tracer(" T_EtatPriseDeService\n");

   memset(PINCode, 0, sizeof(PINCode));
   IdentificationEnCours = true;
   NbEssaisRestant = 0;
   Autorise = false;
   Autorisation = PAR_AGENT_INCONNU;
   Degradee = Degradee_P;
   NbSaisies = 0;
   SaisieEnCours = false;
   if (PorteAvantFermee)
   {
      AEteOuvert_avant = false;
   }
   if (PorteArriere1Fermee && PorteArriere2Fermee)
   {
      AEteOuvert_arriere = false;
   }

   CardHandling = false;

   if ((Degradee_P == IDENT_DEGRADEE_OUVERTURE)) //|| ( (Degradee_P == IDENT_NORMAL ))
   {
      if (!PorteArriere1Fermee || !PorteArriere2Fermee)
      {
         AEteOuvert_arriere = true;
      }
      if (!PorteAvantFermee || !CremonePorteAvantVerrouillee)
      {
         AEteOuvert_avant = true;
      }
   }
   LangueAgent = PAR_LANGUE_INDETERMINEE;

   T_ParLibelleParLangue *Langue_L = NULL;
   const unsigned int NbLangues_L = Par->DonnerLanguesAgent(&Langue_L);
   for (unsigned i = 0; i < NbLangues_L && LangueAgent == PAR_LANGUE_INDETERMINEE; i++)
   {
      if (Langue_L[i].Type == Langue_P)
      {
         LangueAgent = Langue_P;
      }
   }
   if (LangueAgent == PAR_LANGUE_INDETERMINEE)
   {
      LangueAgent = Par->DonnerLangueAgentParDefaut();
   }
}

/*****************************************************************************
* But de la fonction : Destructeur
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
T_EtatPriseDeService::~T_EtatPriseDeService()

{

   IdentificationEnCours = false;

}

/*****************************************************************************
* But de la fonction : Initialiser
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::Initialiser()
{

   /* Tracer */
   switch (Degradee)
   {
   case IDENT_NORMAL:
      TraceAgent.Tracer("+++ Identification avec badge+++.\n");
      break;

   case IDENT_DEGRADEE_OUVERTURE:
      TraceAgent.Tracer("+++ Identification sans badge +++.\n");
      break;

   default:
      TraceAgent.Tracer("+++ Identification speciale +++.\n");
      break;
   }

   /* Arreter temporisation */
   Tempo.ArreterTempo();

   // Arreter la tempo effraction
   // (C'est l'etat de prise de service maintenant les effractions)
   TempoEffractionPorteAvant.ArreterTempo();
   TempoEffractionPorteArriere.ArreterTempo();

   DisplayMessage(DISPLAY_HS);

   /* Activer le verrouillage majuscule */
   Rhm->ActiverVerrouillageMajuscules(VRAI);

   // Arreter la synth�se vocale
   if (ConfigDat->LireIHM().Son && ConfigDat->LireIHM().AideVocaleTTS)
   {
      Rhm->ArreterLectureTTS();
   }

   // Passage dans le bon sens de lecture
   if (LangueAgent == PAR_LANGUE_AR)
   {
      Rhm->ForcerRendu(RHM_RENDU_DROITEAGAUCHE);
   }
   else
   {
      Rhm->ForcerRendu(RHM_RENDU_NORMAL);
   }

   Langue = LangueAgent;

   // R�cup�r�ration du Num exploitant par d�faut (s'il est r�cup�r� de la carte ou saisie, il sera �cras�)
   NoExploitantAgent = Bdd->LireETA().NumeroExploitant;

   // 3 essais d'identification en mode d�grad�
   NbEssaisRestant = 3;

   /* Identification agent en mode nominale avec badge ? */
   if (!Degradee)
   {
      /* Idendification agent avec code de la carte ? */
      if (ConfigDat->LireDAT().CodeIdNominale == T_ConfigDAT::CODE_CARTE)
      {
         /* Si une redetection de carte est demand�e */
         if (RedetectionCarte->Demandee == VRAI)
         {
            /* Tracer usager */
            TraceAgent.Tracer(">>> Redetection carte >>>.\n");

            // Afficher le message
            AfficherEcranConsultationCarte(PRESENTER_CARTE);

            /* Arr�ter la detection */
            ArreterDetectionCarte(AVANT);
            ArreterDetectionCarte(ARRIERE);

            /* Relancer la detection sur toutes les antennes. Seules celles configur�es seront effectivement activ�es.*/
            if (DetectionTlbAvant)
            {
               DetecterCarte(AVANT);
            }
            else if (DetectionTlbArriere)
            {
               DetecterCarte(ARRIERE);
            }

            /* Armer temporisation d'inaction */
            Rhm->ArmerTempoInaction(Par->DonnerTempoInactionClient());
         }
         else
         {
            /* Afficher contr�le en cours */
            AfficherControleEnCours();

            /* Lire nombre d'essais de saisie du code PIN */
            if (DetectionTlbAvant)
            {
               LireEtatsInvalidationEtPinCode(AVANT);
            }
            else if (DetectionTlbArriere)
            {
               LireEtatsInvalidationEtPinCode(ARRIERE);
               ;
            }
         }
      }

      /* Identification agent nominale avec badge et code PIN  ? */
      else if (ConfigDat->LireDAT().CodeIdNominale == T_ConfigDAT::CODE ||
         ConfigDat->LireDAT().CodeIdNominale == T_ConfigDAT::CODE_DU_JOUR)
      {
         /* Afficher l'�cran de saisie du code pin */
         AfficherEcranSaisie();
      }

      /* Identification agent sans code */
      else
      {
         /* Lire les donn�es agent sans code PIN */
         T_ApiTlbDdeLectureDonneesAgent DdeLecture_L = { 0 };
         LireDonneesAgent(AVANT, DdeLecture_L);
      }
   }
   else
   {
      // Identification sans badge

      // Identification sur simple ouverture de la porte sans saisie avec num�ro d'agent par d�faut ? (pour les demos)
      if (ConfigDat->LireDAT().CodeIdDegradee == T_ConfigDAT::SANS_CODE && ConfigDat->LireDAT().SaisieIdDegradee == T_ConfigDAT::SANS_SAISIE)
      {
         // Auto-renseignement de l'exploitant et agent
         NoAgent = ConfigDat->LireDAT().FonctionsAgentNoAgent;
         if (ConfigDat->LireDAT().FonctionsAgentNoExploitantAgent)
         {
            NoExploitantAgent = ConfigDat->LireDAT().FonctionsAgentNoExploitantAgent;
         }

         if (AgentAutorise())
         {
            /* Acc�s autoris� */
            AfficherAutorisation();
            TraiterFinPriseDeService();
         }
         else
         {
            // Identification d�grad�e avec saisie du num�ro d'exploitant, num�ro d'agent et �ventuelement du code PIN calcul� par algorithme
            AfficherEcranSaisie();
         }
      }
      else
      {
         /*if (ConfigDat->LireDAT ().SaisieIdDegradee == T_ConfigDAT::SAISIE_NO_AGENT)
         {
            // Pas de saisie du num�ro d'exploitant : on le r�cup�re
            NoExploitantAgent = Bdd->LireETA ().NumeroExploitant;
         }*/


         // Identification d�grad�e avec saisie du num�ro d'exploitant, num�ro d'agent et �ventuelement du code PIN calcul� par algorithme
         AfficherEcranSaisie();
      }
   }
}

bool T_EtatPriseDeService::IsDoorOpen()
{
   bool isDoorOpen = false;

   if (ConfigDat->LireES().AccesArriere)
   {
      isDoorOpen = !(PorteArriere1Fermee && PorteArriere2Fermee);
   }

   if (!isDoorOpen && ConfigDat->LireES().AccesAvant)
   {
      isDoorOpen = !PorteAvantFermee;
   }

   // Backdoor timeout
   if (!isDoorOpen && DetectionTlbArriere)
   {
      isDoorOpen = true;
   }

   return isDoorOpen;
}

/*****************************************************************************
* But de la fonction : Afficher �cran de saisie num�ro agent et code PIN
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::AfficherEcranSaisie(void)

{

   // Saisie en cours
   NbSaisies = 0;
   SaisieEnCours = true;

   switch (NbEssaisRestant)
   {
   case 3:
      Par->DonnerLibelleAgent(LibelleFonction, "L1613", LangueAgent); // "Premier essai"
      break;

   case 2:
      Par->DonnerLibelleAgent(LibelleFonction, "L1614", LangueAgent); // "Deuxi�me essai"
      break;

   case 1:
      Par->DonnerLibelleAgent(LibelleFonction, "L1615", LangueAgent); // "Troisi�me et dernier essai"
      break;

   default:
      Par->DonnerLibelleAgent(LibelleFonction, "L1627", LangueAgent); //"Identification agent"
      break;
   }

   // Construction de la ligne de service
   memset(&EcranSaisie, 0, sizeof EcranSaisie);
   ConstruireEnteteIntervention(EcranSaisie.Entete);

   // Nombre de saisie
   EcranSaisie.NbSaisie = 0;



   if (ConfigDat->LireIHM().TypeMaintenance == T_ConfigIHM::ECRAN_PAYSAGE_ET_BOUTONS && IsDoorOpen())
   {
      // En IHM 2, pas de dalle tactile, pas de pav� num�rique       
      EcranSaisie.PaveNumeriquePresent = FAUX;
   }
   else
   {
      EcranSaisie.PaveNumeriquePresent = VRAI;
   }

   // display on appropriate screen according to door state
   if (IsDoorOpen())
   {
      Rhm->ChangerSortieEcran(VRAI);
   }
   else
   {
      Rhm->ChangerSortieEcran(FAUX);
   }

   if (Degradee)
   {
      // Saisir le num�ro d'exploitant ou �chec identification sans saisie ?
      if (ConfigDat->LireDAT().SaisieIdDegradee == T_ConfigDAT::SAISIE_NO_EXPLOITANT
         || ConfigDat->LireDAT().SaisieIdDegradee == T_ConfigDAT::SANS_SAISIE
         // Si on n'est pas initialis� (donc pas de CEB) et qu'on ne saisit pas le num exploitant, on ne peut pas le deviner !
         || !Bdd->Initialisee())

      {
         // En d�grad� saisir le num�ro d'exploitant
         EcranSaisie.Saisie[EcranSaisie.NbSaisie].NbCaractere = 3;
         EcranSaisie.Saisie[EcranSaisie.NbSaisie].EchoSecret = FAUX; // On ne masque pas le num�ro d'exploitant, et on met la valeur connue
         EcranSaisie.Saisie[EcranSaisie.NbSaisie].TypeSaisie = SAISIE_NOMBRE;
         Par->DonnerLibelleAgent(EcranSaisie.Saisie[EcranSaisie.NbSaisie].Libelle, "L1631", LangueAgent); //"Num�ro exploitant"
         if (NoExploitantAgent)
         {
            sprintf(EcranSaisie.Saisie[EcranSaisie.NbSaisie].ChaineDefaut, "%3.3u", NoExploitantAgent);
         }
         EcranSaisie.NbSaisie++;
      }

      // En d�grad� saisir le num�ro d'agent
      EcranSaisie.Saisie[EcranSaisie.NbSaisie].NbCaractere = 10;
      EcranSaisie.Saisie[EcranSaisie.NbSaisie].EchoSecret = VRAI;
      EcranSaisie.Saisie[EcranSaisie.NbSaisie].TypeSaisie = SAISIE_NOMBRE;
      Par->DonnerLibelleAgent(EcranSaisie.Saisie[EcranSaisie.NbSaisie].Libelle, "L1629", LangueAgent); //"Num�ro de matricule"
      EcranSaisie.NbSaisie++;
   }

   if (!Degradee
      || ConfigDat->LireDAT().CodeIdDegradee == T_ConfigDAT::CODE
      || ConfigDat->LireDAT().CodeIdDegradee == T_ConfigDAT::CODE_DU_JOUR
      || ConfigDat->LireDAT().CodeIdDegradee == T_ConfigDAT::CODE_PARAMETRE)
   {
      // En nominal et �ventuelement en d�grad� saisir le code PIN
      EcranSaisie.Saisie[EcranSaisie.NbSaisie].NbCaractere = 4;
      EcranSaisie.Saisie[EcranSaisie.NbSaisie].EchoSecret = VRAI;
      EcranSaisie.Saisie[EcranSaisie.NbSaisie].TypeSaisie = SAISIE_NOMBRE;
      Par->DonnerLibelleAgent(EcranSaisie.Saisie[EcranSaisie.NbSaisie].Libelle, "L1630", LangueAgent); // "Code PIN"
      EcranSaisie.NbSaisie++;
   }

   // Boutons associ�s au pav� num�rique
   EcranSaisie.Entete.BtValidation.Etat = RHM_DISPONIBLE;
   Par->DonnerLibelleAgent(EcranSaisie.Entete.BtValidation.Libelle, "L0048", LangueAgent); // "Confirmez la saisie"
   EcranSaisie.BtAnnul.Etat = RHM_DISPONIBLE;
   Par->DonnerLibelleAgent(EcranSaisie.BtAnnul.Libelle, "L1638", LangueAgent); // "Annuler"
   EcranSaisie.BtCorr.Etat = RHM_DISPONIBLE;
   Par->DonnerLibelleAgent(EcranSaisie.BtCorr.Libelle, "L1637", LangueAgent); // "Corriger"
   EcranSaisie.BtValid.Etat = RHM_DISPONIBLE;
   Par->DonnerLibelleAgent(EcranSaisie.BtValid.Libelle, "L1636", LangueAgent); // "Valider"
   EcranSaisie.BtSuivant.Etat = RHM_DISPONIBLE;
   Par->DonnerLibelleAgent(EcranSaisie.BtSuivant.Libelle, "L0053", LangueAgent); // "Suivant"

   // Demande d'affichage de l'ecran
   Rhm->AfficherSaisie(EcranSaisie);

   /* Armer temporisation d'inaction */
   Rhm->ArmerTempoInaction(Par->DonnerTempoAgentLogin());

}

/*****************************************************************************
* But de la fonction : Afficher
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::AfficherControleEnCours(void)
{
   // "Identification agent"
   Par->DonnerLibelleAgent(LibelleFonction, "L1627", LangueAgent);

   // display on appropriate screen according to door state
   if (IsDoorOpen())
   {
      Rhm->ChangerSortieEcran(VRAI);
   }
   else
   {
      Rhm->ChangerSortieEcran(FAUX);
   }

   /* Afficher "Patientez s'il vous plait, V�rification de l'autorisation..." */
   memset(&EcranConsul, 0, sizeof EcranConsul);
   ConstruireEnteteIntervention(EcranConsul.Entete);
   Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[2], "L1634", LangueAgent); // "Patientez s'il vous plait"
   Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[3], "L1635", LangueAgent); // "V�rification de l'autorisation..."
   EcranConsul.NbLignes = 4;
   EcranConsul.TaillePolice = 12;
   EcranConsul.Alignement = RHM_TEXTE_CENTRE;
   EcranConsul.CentreVertical = VRAI;

   Rhm->AfficherConsultation(EcranConsul);
}

/*****************************************************************************
* But de la fonction : Afficher autorisation
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::AfficherAutorisation(void)

{
   unsigned int iNoLigne = 1;

   // "Identification agent"
   Par->DonnerLibelleAgent(LibelleFonction, "L1627", LangueAgent);

   // On construit la ligne de service
   memset(&EcranConsul, 0, sizeof EcranConsul);
   ConstruireEnteteIntervention(EcranConsul.Entete);

   switch (Autorisation)
   {
   case PAR_AGENT_AUTORISE:

      switch (TypeIntervention)
      {
      case 'E':
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1619", LangueAgent); // "Intervention d'exploitation autoris�e"
         break;

      case 'M':
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1620", LangueAgent); // "Intervention de maintenance autoris�e"
         break;

      case 'R':
      case 'C':
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1621", LangueAgent); // "Intervention de recette autoris�e"
         break;

      default:
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1622", LangueAgent); // "Intervention autoris�e"
         break;
      }
      break;

   case PAR_AGENT_PIN_ERROR:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1617", LangueAgent); // "Erreur de saisie du code"
      break;

   case PAR_AGENT_INCONNU:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1623", LangueAgent); // "Agent inconnu"
      break;

   case PAR_AGENT_SANS_DROIT:
   default:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1624", LangueAgent); // "Agent non autoris�"
      break;
   }

   if (!Degradee && CarteSansContactPresente)
   {
      if (ConfigDat->LireCSM().PossibiliteRetraitSupportSC == VRAI && RedetectionCarte->Demandee == VRAI)
      {
         /* Tracer usager */
         Usager.Tracer(">>> Redetection carte >>>.\n");

         /* Arr�ter la detection */
         ArreterDetectionCarte(AVANT);

         /* Relancer la detection */
         DetecterCarte(AVANT);

         /* Armer tempo de redectetion */
         Tempo.Armer(3000);
      }
      else
      {
         /* Armer temporisation d'inaction */
         Rhm->ArmerTempoInaction(Par->DonnerTempoInactionClient());

         /* Activer buzzer pour attente retrait carte */
         ActiverBuzzer();

         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1609", LangueAgent); // "Veuillez retirer votre carte"

         // Allumer led verte
         TlbAttenteRetrait(VRAI);
      }
   }

   if (Degradee && !ClefDesactive && ConfigDat->LireES().ClePsAgent)
   {
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[iNoLigne++], "L1632", LangueAgent); // "D�sactivez le bouton vert"
   }

   // On construit la ligne a afficher
   EcranConsul.NbLignes = 4;
   EcranConsul.TaillePolice = 12;
   EcranConsul.Alignement = RHM_TEXTE_CENTRE;
   EcranConsul.CentreVertical = VRAI;

   // Demande d'affichage a la RHM
   Rhm->AfficherConsultation(EcranConsul);

}

/*****************************************************************************
* But de la fonction : Afficher l'ecran de prise de service
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::AfficherEcranConsultationCarte(unsigned int TypeAffichage_P, char *CodePin_P)

{
   unsigned int i = 3;
   T_RhmLibelleAgent Libelle_L;

   if (CodePin_P == NULL)
   {
      CodePin_P = "";
   }

   /* Initialiser l'affichage */
   memset(&EcranConsul.LibelleLigne, 0, sizeof EcranConsul.LibelleLigne);

   Par->DonnerLibelleAgent(LibelleFonction, "L1627", LangueAgent); //"Identification agent"

   // On construit la ligne de service
   memset(&EcranConsul, 0, sizeof EcranConsul);
   ConstruireEnteteIntervention(EcranConsul.Entete);

   Par->DonnerLibelleAgent(Libelle_L, "L1609", LangueAgent); // "Veuillez retirer votre carte"

   switch (TypeAffichage_P)
   {
   case PRESENTER_CARTE:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1604", LangueAgent); // "Veuillez remettre votre carte"
      break;

   case PSCARTE_CARTE_INCONNUE:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1605", LangueAgent); //"Carte inconnue"
      i += 1;
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case PSCARTE_CARTE_EXPIREE:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1606", LangueAgent); // "Carte expir�e"
      i += 1;
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case PSCARTE_CARTE_BLOQUEE:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1608", LangueAgent); // "Carte bloqu�e"
      i += 1;
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case VERIF_NB_ESSAIS:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1610", LangueAgent); // "Contr�les en cours"
      break;

   case PROBLEME_SAISIE_CODE:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1617", LangueAgent); // "Erreur de saisie du code"
      i += 1;
      if (CarteSansContactPresente)
      {
         _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      }
      break;

   case IDENTIFICATION_INDISPONIBLE:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1618", LangueAgent); // "Identification par badge indisponible"
      i += 1;
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case AUTORISATION:
      switch (TypeIntervention)
      {
      case 'E':
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1619", LangueAgent); //"Intervention d'exploitation autoris�e"
         _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
         break;

      case 'M':
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1620", LangueAgent); //"Intervention de maintenance autoris�e"
         _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
         break;

      case 'R':
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1621", LangueAgent); //"Intervention de recette autoris�e"
         _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
         break;

      default:
         Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1622", LangueAgent); //"Intervention autoris�e"
         _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
         break;
      }
      break;

   case AGENT_INCONNU:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1623", LangueAgent); //"Agent inconnu"
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case AGENT_SANS_DROIT:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1624", LangueAgent); //"Agent non autoris�"
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case PROBLEME_LECTURE_CARTE:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1625", LangueAgent); //"Probl�me de lecture carte"
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case RETRAIT_CARTE:
      _snprintf_s(EcranConsul.LibelleLigne[i++], WSIZE(EcranConsul.LibelleLigne[0]), _TRUNCATE, Libelle_L);
      break;

   case ATTENTE_CARTE_AGENT:
      Par->DonnerLibelleAgent(EcranConsul.LibelleLigne[i++], "L1626", LangueAgent); //"Veuillez pr�senter votre badge agent"
      break;
   }

   /* Recuperer le nombre de lignes */
   EcranConsul.NbLignes = i;

   /* Si l'affichage demande un retrait carte */
   if (TypeAffichage_P != SAISIE_CODE && TypeAffichage_P != VERIF_NB_ESSAIS && TypeAffichage_P != ATTENTE_CARTE_AGENT && TypeAffichage_P != PRESENTER_CARTE)
   {
      if (CarteSansContactPresente)
      {
         /* Armer temporisation d'inaction */
         Rhm->ArmerTempoInaction(Par->DonnerTempoInactionClient());

         /* Activer buzzer pour attente retrait carte */
         ActiverBuzzer();

         if (!Autorise)
         {
            // If agent did not identify successfully, raise alarm
            if (!PorteAvantFermee || AEteOuvert_avant)
            {
               TraiterPresenceAlarme(BDD_INDEX_PT, COD_EFFRACTION_PORTE_AVANT);
            }

            if (!PorteArriere1Fermee || !PorteArriere2Fermee || AEteOuvert_arriere)
            {
               TraiterPresenceAlarme(BDD_INDEX_PT, COD_EFFRACTION_PORTE_ARRIERE);
            }
         }

         //if (DetectionTlbArriere)
         //{
         //   /* If the agent does not enter the PIN in 10 seconds of time */
         //   TraiterPresenceAlarme(BDD_INDEX_PT, COD_EFFRACTION_PORTE_AVANT);
         //}
      }
   }

   /* Fixer la taille de la police */
   EcranConsul.TaillePolice = 12;
   EcranConsul.CentreVertical = VRAI;
   EcranConsul.Alignement = RHM_TEXTE_CENTRE;

   /* Afficher l'ecran */
   Rhm->AfficherConsultation(EcranConsul);
}

/*****************************************************************************
* But de la fonction : Preparation de l'ecran de prise de service intervention
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
bool T_EtatPriseDeService::AgentAutorise(void)

{
   bool IdentSousContrainte_L = false;

   /* Afficher contr�le en cours */
   AfficherControleEnCours();

   /* Agent autoris� ? */
   Autorisation = Agent->DonnerAutorisation(NoAgent, NoExploitantAgent);

   if (Autorisation == PAR_AGENT_INIT)
   {
      TraceAgent.Tracer("** Pas d'agent - pas de parametre ***.\n");
      Autorisation = PAR_AGENT_AUTORISE;
   }
   else if (Autorisation == PAR_AGENT_AUTORISE)
   {

      /* Identification agent nominale avec badge et code PIN calcul� par algorithme */
      /* Identification d�grad�e et Mode d�authentification nominal = 2 : Authentification par saisie manuelle */
      /* Identification d�grad�e avec saisie du num�ro d'agent et du code PIN calcul� par algorithme */
      if ((!Degradee && ConfigDat->LireDAT().CodeIdNominale == T_ConfigDAT::CODE) ||
         (Degradee && Par->DonnerModeAuthentNominal() == PAR_MODE_AUTHENT_NOMINAL_PAR_SAISIE) ||
         (Degradee && ConfigDat->LireDAT().CodeIdDegradee == T_ConfigDAT::CODE))
      {
         if (Agent->ControlerCode(NoAgent, NoExploitantAgent, atoi(PINCode)) != OK)
         {
            Autorisation = PAR_AGENT_PIN_ERROR;
            TraceAgent.Tracer("Code jour fixe agent %u/%u *** Code faux ***.\n", NoExploitantAgent, NoAgent);
         }
      }

      /* Identification agent nominale avec badge et code PIN calcul� par algorithme */
      /* Identification d�grad�e avec saisie du num�ro d'agent et du code PIN calcul� par algorithme */
      else if ((!Degradee && ConfigDat->LireDAT().CodeIdNominale == T_ConfigDAT::CODE_DU_JOUR) ||
         (Degradee && ConfigDat->LireDAT().CodeIdDegradee == T_ConfigDAT::CODE_DU_JOUR))
      {
         if (Agent->ControlerCode(NoAgent, NoExploitantAgent, atoi(PINCode), VRAI) != OK)
         {
            Autorisation = PAR_AGENT_PIN_ERROR;
            TraceAgent.Tracer("Code du jour agent %u/%u *** Code faux ***.\n", NoExploitantAgent, NoAgent);
         }
      }

      /* Identification agent sans badge et num�ro agent / code PIN via param�tres */
      else if (Degradee && ConfigDat->LireDAT().CodeIdDegradee == T_ConfigDAT::CODE_PARAMETRE)
      {
         char CodeNipAvant_L[32 + 1] = { 0 };
         char CodeNipCrypte_L[32 + 1] = { 0 };

         sprintf(CodeNipAvant_L, "%3.3u%6.6u00000000%4.4s", NoExploitantAgent, NoAgent, PINCode);
         // MD5
         strncpy(CodeNipCrypte_L, md5(CodeNipAvant_L), sizeof CodeNipCrypte_L);

         if (Par->NBA_EstCeNipOK(NoAgent, NoExploitantAgent, CodeNipCrypte_L) == false)
         {
            TraceAgent.Tracer("Code param agent %u/%u *** Code faux***\n", NoExploitantAgent, NoAgent);

            // Code parametr� sans badge KO, peut �tre c'est le code du jour ?
            if (Agent->ControlerCode(NoAgent, NoExploitantAgent, atoi(PINCode), VRAI) != OK)
            {
               Autorisation = PAR_AGENT_PIN_ERROR;
               TraceAgent.Tracer("Code du jour *** Code faux *** aussi\n");
            }
         }
      }


      // En nominal : Code secret agression agent (KVMRT)
      // ATTENTION : ne fonctionne pas avec le code control� par la carte (car il faut donner le code pin pour acc�der aux infos carte!)
      if (!Degradee && ConfigDat->LireDAT().CodeSecretAgentSousContrainte &&
         (ConfigDat->LireDAT().CodeSecretAgentSousContrainte == atoi(PINCode)))
      {
         TraceAgent.Tracer("CODE SECRET SOUS LA CONTRAINTE\n");

         // Identification agent sous la contrainte
         IdentSousContrainte_L = true;

         // L'identification est per�ue comme r�ussie
         Autorisation = PAR_AGENT_AUTORISE;
      }
   }

   // Autoris� ?
   if (Autorisation == PAR_AGENT_AUTORISE)
   {
      Autorise = true;

      Es->DesactiverSirene();

      TraceAgent.Tracer("Agent %u autorise ***.\n", NoAgent);

      if (IdentSousContrainte_L)
      {
         TraiterPresenceAlarme(BDD_INDEX_AG, COD_CONNEXION_AGENT_SOUS_CONTRAINTE);
      }
      else
      {
         TraiterAbsenceAlarme(BDD_INDEX_AG, COD_CONNEXION_AGENT_SOUS_CONTRAINTE);
      }

      /* Acquerir type d'intervention */
      TypeIntervention = Agent->DonnerTypeIntervention();

   }
   else if (Autorisation == PAR_AGENT_PIN_ERROR)
   {
      /* Tracer */
      TraceAgent.Tracer("*** Code faux ***.\n");
   }
   else if (Autorisation == PAR_AGENT_INCONNU)
   {
      /* Tracer */
      TraceAgent.Tracer("*** Agent %u inconnu ***.\n", NoAgent);
   }
   else
   {
      /* Tracer */
      TraceAgent.Tracer("*** Agent %u non autorise ***.\n", NoAgent);
   }

   /* Fin de la fonction */
   return Autorise;

}

/*****************************************************************************
* But de la fonction : Abandonne la prise de service
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::TraiterAbandonPriseDeService(void)
{

   if (Degradee)
   {
      Autorise = FAUX;
      TraiterFinPriseDeService();
   }
   else
   {
      if (!CarteSansContactPresente && !TicketSansContactPresent)
      {
         TraiterFinPriseDeService();
      }
      else if (ConfigDat->LireCSM().PossibiliteRetraitSupportSC == VRAI
         && Par->DonnerModeAuthentNominal() == PAR_MODE_AUTHENT_NOMINAL_PAR_BADGE_OU_SAISIE)
      {
         T_GT_CompteRendu Cr_L = EstCeUneCarteValide();

         if (Cr_L == GT_OK)
         {
            // On retourne en consultation
            new T_EtatCscConsultation();
         }
         else
         {
            // Afficher retirer carte
            AfficherEcranConsultationCarte(RETRAIT_CARTE);
         }
      }
      else
      {
         // Afficher retirer carte
         AfficherEcranConsultationCarte(RETRAIT_CARTE);
      }
   }

}

/*****************************************************************************
* But de la fonction : Affichage du menu agent si autorisation, sinon HS vente
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::TraiterFinPriseDeService(void)

{
   if (Autorise)
   {
      T_BddIDA IDA_L;

      /* Tracer */
      TraceAgent.Tracer("--- Identification agent %u ---.\n", NoAgent);

      if (Degradee)
      {
         Bdd->EcrireETA(TypeIntervention);
         // Ev�nement identification sans badge agent
         TraiterPresenceAlarme(BDD_INDEX_AG, COD_CONNEXION_AGENT_SANS_BADGE);
         TraiterAbsenceAlarme(BDD_INDEX_AG, COD_CONNEXION_AGENT_SANS_BADGE);
      }

      /* Identification avec ou sans badge ? */
      Bdd->LireIDA(IDA_L);
      IDA_L.IdentificationAvecBadge = (!Degradee) ? VRAI : FAUX;
      Bdd->EcrireIDA(IDA_L);

      /* Arreter la d�tection carte pour les �crans agents */
      ArreterDetectionCarte(AVANT);
      ArreterDetectionCarte(ARRIERE);

      if (CardHandling && ConfigDat->LireDAT().ShowInterventionScreenUntilDoorIsOpen)
      {
         // Agent screen must be displayed on internal display, waiting for the agent to open any door.
         // --> the (external) sale screen must display a "intervention in progress" display, locking external interface.
         CardHandling = false;
         Rhm->ChangerSortieEcran(VRAI, true);
      }

      // Chgt etat : Etat liste des fonctions
      new T_EtatListeFonctions(VRAI, Degradee);
   }
   else
   {
      /* Tracer */
      TraceAgent.Tracer("--- Identification echouee ---.\n");

      /* Evenement �chec identification */
      if (Autorisation != PAR_AGENT_AUTORISE)
      {
         TraiterPresenceAlarme(BDD_INDEX_AG, Autorisation);
         TraiterAbsenceAlarme(BDD_INDEX_AG, Autorisation);
      }

      // Si la porte a �t� ouverte (meme si referm�e depuis), effraction s'il n'y a pas eu d'ident OK
      if (AEteOuvert_avant)
      {
         /* Effraction porte avant */
         TraiterPresenceAlarme(BDD_INDEX_PT, COD_EFFRACTION_PORTE_AVANT);
      }
      if (AEteOuvert_arriere)
      {
         /* Effraction porte arriere */
         TraiterPresenceAlarme(BDD_INDEX_PT, COD_EFFRACTION_PORTE_ARRIERE);
      }

      /* Mise en �tat hors service */
      new T_EtatHorsService();
   }

}

/*****************************************************************************
* But de la fonction : Traiter le nombre d'essais restant du code pin sur la carte agent
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::TraiterNbEssaisCodePin(const T_ApiTlbCrLectureEtatsInvalidationEtPinCode *Etat_P)

{
   /* Si compte rendu correcte */
   if (Etat_P->CompteRendu == OK)
   {
      /* Si le nombre d'essais est d�pass� ou la carte invalid�e */
      if (Etat_P->NbEssaisPinCodeRestants == 0 || Etat_P->EtatInvalidation == APIBUF_CARTE_INVALIDE)
      {
         /* Tracer */
         TraceAgent.Tracer("*** Carte bloquee ***.\n");

         /* Afficher le message */
         AfficherEcranConsultationCarte(PSCARTE_CARTE_BLOQUEE);
      }
      else
      {
         NbEssaisRestant = Etat_P->NbEssaisPinCodeRestants;

         /* Afficher l'�cran de saisie du code pin */
         AfficherEcranSaisie();
      }
   }
   else
   {
      /* Tracer */
      TraceAgent.Tracer("*** Erreur lecture carte ***.(CR=%d)\n\n", Etat_P->CompteRendu);

      /* Afficher le message */
      AfficherEcranConsultationCarte(PROBLEME_LECTURE_CARTE);
   }

}

/*****************************************************************************
* But de la fonction : Traiter le compte rendu de lecture de la carte agent
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::TraiterCrLectureCarteAgent(const T_ApiTlbCrLectureDonneesAgent *CrLecture_P)

{

   if (CrLecture_P->CompteRendu == OK)
   {

      if (Par->DonnerModeAuthentNominal() == PAR_MODE_AUTHENT_NOMINAL_PAR_BADGE_OU_SAISIE)
      {
         // La carte ne porte pas le num�ro agent / Exploitant !
         unsigned long ulNoSerie_L = GetIDAdentAMC();

         // Trouve le num agent / exploitant en fonction du num�ro AMC lu dans la carte
         // 8 = Num�ro d'unicit� de l'AMC
         if (Par->NBA_EstCeAgent(8, ulNoSerie_L, &NoAgent, &NoExploitantAgent) == false)
         {
            TraceAgent.Tracer("*** ID %lu ne correspond pas a un agent ***.\n", ulNoSerie_L);
         }
      }
      else
      {
         // Carte agent classique
         // Memoriser le num�ro agent
         NoAgent = CrLecture_P->DonneesTitre.DonneesAgent.IDAgentID;

         for (unsigned char i = 0; (i < CrLecture_P->NbDonneesCodees) && (i < NB_MAX_DONNEES_CARTE); i++)
         {
            unsigned short usValue = CrLecture_P->DonneesCodees[i];
            if (usValue == NUM_ID_AGENT_COMPAGNY)
            {
               NoExploitantAgent = CrLecture_P->DonneesTitre.DonneesAgent.IDAgentCompagny;
               break;
            }
            else if (usValue == NUM_ID_AGENT_COMPAGNY_EXTENDED)
            {
               NoExploitantAgent = CrLecture_P->DonneesTitre.DonneesAgent.IDAgentCompagnyExtended;
               break;
            }
         }
      }
      CardHandling = true;
      AgentAutorise();
      AfficherAutorisation();
   }

   else if (CrLecture_P->CompteRendu == PREMIER_CODE_INCORRECT || CrLecture_P->CompteRendu == DEUXIEME_CODE_INCORRECT)
   {
      /* Tracer */
      TraceAgent.Tracer("*** Code faux ***\n");

      NbEssaisRestant--;
      AfficherEcranSaisie();
   }

   else if (CrLecture_P->CompteRendu == CARTE_VERROUILLEE)
   {
      /* Tracer */
      TraceAgent.Tracer("*** Carte bloquee ***\n");

      Autorisation = PAR_AGENT_PIN_ERROR;
      AfficherEcranConsultationCarte(PSCARTE_CARTE_BLOQUEE);
   }
   else
   {
      /* Tracer */
      TraceAgent.Tracer("*** Erreur lecture carte *** (CR=%d)\n", CrLecture_P->CompteRendu);

      /* Type de message a afficher */
      AfficherEcranConsultationCarte(PROBLEME_SAISIE_CODE);
   }

   if (!CarteSansContactPresente)
   {
      TraiterFinPriseDeService();
   }

}

/*****************************************************************************
* But de la fonction : Traiter saisie
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::TraiterCrSelection(const T_RhmCrSelection *CrSelection_P)

{

   //*** Saisie en cours ? */
   if (SaisieEnCours)
   {
      //*** Compte_rendu de validantion */
      if (CrSelection_P->CompteRendu == VALIDATION)
      {
         if (Degradee)
         {
            NbSaisies++;

            // Num�ro d'exploitant ou num�ro d'agent
            if (NbSaisies == 1)
            {
               // Saisir le num�ro d'exploitant
               // ou en cas d'�chec identification sans saisie
               if (ConfigDat->LireDAT().SaisieIdDegradee == T_ConfigDAT::SAISIE_NO_EXPLOITANT
                  || ConfigDat->LireDAT().SaisieIdDegradee == T_ConfigDAT::SANS_SAISIE
                  || !Bdd->Initialisee())
               {
                  // Il s'agit du Num�ro d'exploitant
                  NoExploitantAgent = atoi(CrSelection_P->Saisie);
               }
               else
               {
                  // Il s'agit du Num�ro d'agent
                  NoAgent = atoi(CrSelection_P->Saisie);
               }
            }

            // Num�ro d'agent ou code PIN
            else if (NbSaisies == 2)
            {
               // Saisir le num�ro d'exploitant 
               // ou en cas d'�chec identification sans saisie
               if (ConfigDat->LireDAT().SaisieIdDegradee == T_ConfigDAT::SAISIE_NO_EXPLOITANT
                  || ConfigDat->LireDAT().SaisieIdDegradee == T_ConfigDAT::SANS_SAISIE
                  || !Bdd->Initialisee())
               {
                  // Il s'agit du Num�ro d'agent
                  NoAgent = atoi(CrSelection_P->Saisie);
               }
               else
               {
                  // Il s'agit du Code PIN
                  strcpy(PINCode, CrSelection_P->Saisie);
               }
            }

            // NbSaisies == 3 : forc�ment code PIN
            else
            {
               // Code PIN
               strcpy(PINCode, CrSelection_P->Saisie);
            }
         }
         else
         {
            // Code PIN identification avec badge
            strcpy(PINCode, CrSelection_P->Saisie);

            // Si rien saisie en code PIN, on ne le comptabilise pas
            if (strlen(PINCode) > 0)
            {
               NbSaisies++;
            }
         }

         if (NbSaisies == EcranSaisie.NbSaisie)
         {
            // La saisie est termin�e
            SaisieEnCours = false;

            if (Degradee)
            {
               // On effectue la prise de service
               if (AgentAutorise() || NbEssaisRestant == 1)
               {
                  AfficherAutorisation();

                  /* Si la cl� est d�sactiv�e ou que l'�quipement est une borne */
                  if (ClefDesactive || ConfigDat->LireES().ClePsAgent == FAUX)
                  {
                     TraiterFinPriseDeService();
                  }
                  else
                  {
                     /* Armer temporisation d'inaction */
                     /* Attente d�sactivation bouton vert */
                     Rhm->ArmerTempoInaction(Par->DonnerTempoInactionIdentification());
                  }
               }
               else
               {
                  /* Identification d�grad�e avec saisie du num�ro d'exploitant, num�ro d'agent et �ventuelement du code PIN calcul� par algorithme */
                  NbEssaisRestant--;
                  AfficherEcranSaisie();
               }
            }
            else
            {
               T_ApiTlbDdeLectureDonneesAgent DdeLecture_L = { 0 };

               if (ConfigDat->LireDAT().CodeIdNominale == T_ConfigDAT::CODE_CARTE)
               {
                  // Lecture avec code PIN
                  memcpy(DdeLecture_L.CodePin, PINCode, sizeof DdeLecture_L.CodePin);
                  DdeLecture_L.CodePinUsed = VRAI;
               }

               // Lire les donn�es agent 
               if (DetectionTlbAvant)
               {
                  LireDonneesAgent(AVANT, DdeLecture_L);
               }
               else if (DetectionTlbArriere)
               {
                  LireDonneesAgent(ARRIERE, DdeLecture_L);
               }
            }
         }
         else
         {
            /* Armer temporisation d'inaction */
            Rhm->ArmerTempoInaction(Par->DonnerTempoAgentLogin());

            if (NbSaisies == 0)
            {
               AfficherEcranSaisie();
            }
         }
      }

      //*** Compte_rendu d'annulation */
      else if (CrSelection_P->CompteRendu == ANNULATION)
      {
         /* Tracer */
         TraceAgent.Tracer(">>> ANNULATION >>>.\n");

         TraiterAbandonPriseDeService();
      }
   }

}

/*****************************************************************************
* But de la fonction : Traiter la presence d'une carte Agent par defaut
* --------------------
* Parametres :
* ------------
* cartes : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
unsigned int T_EtatPriseDeService::TraiterCarteAgent(void)

{
   unsigned int Return_L;

   if (Degradee)
   {
      IncrementerCtrCartesAcceptees(AVANT);

      Degradee = IDENT_NORMAL;
      Initialiser();

      // Evenement trait�
      Return_L = VRAI;
   }
   else
   {
      // Evenement non trait�
      Return_L = VRAI;

   }

   // Fin de la fonction
   return Return_L;

}

/*****************************************************************************
* But de la fonction : Traiter le retrait de la carte telebillettique
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
unsigned int T_EtatPriseDeService::TraiterRetraitCarteTLB(void)

{
   unsigned int Return_L = VRAI;

   if (!Degradee)
   {
      /* Tracer */
      TraceAgent.Tracer(">>> Retrait carte sans contact >>>.\n");

      /* Arreter le buzzer */
      DesactiverBuzzer();

      TraiterFinPriseDeService();
   }
   else
   {
      /* Ev�nement non trait� */
      Return_L = FAUX;
   }

   /* Fin de la fonction */
   return Return_L;

}

/*****************************************************************************
* But de la fonction : Traitement des evenements specifiques de l'etat
* -------------------- ouverture porte
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
unsigned int T_EtatPriseDeService::TraiterDesactivationClef(void)

{
   unsigned int Return_L;

   if (Degradee)
   {
      /* Tracer */
      TraceAgent.Tracer(">>> Desactivation cle agent >>>.\n");

      TraiterFinPriseDeService();

      /* Ev�nement trait� */
      Return_L = VRAI;
   }
   else
   {
      /* Ev�nement non trait� */
      Return_L = FAUX;
   }

   /* Fin de la fonction */
   return Return_L;

}

/*****************************************************************************
* But de la fonction : Traiter les evenements asynchrones
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
void T_EtatPriseDeService::TraiterEvenementsAsynchrones(void)

{

   if (Degradee)
   {
      T_Etat::TraiterEvenementsAsynchrones();
   }
   else
   {
      /* Les �v�nements asynchrones sont masqu�s */
   }

}

/*****************************************************************************
* But de la fonction : Traitement des evenements specifiques de l'etat
* -------------------- ouverture porte
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
unsigned int T_EtatPriseDeService::TraiterEtat(void)

{
   unsigned int Return_L = VRAI;
   T_ApiTlbCrLectureEtatsInvalidationEtPinCode Etat_L;
   T_ApiTlbCrLectureDonneesAgent CrLecture_L;
   T_RhmCrSelection CrSelection_L;

   /*** Compte-rendu lecture nombre d'essais de saisie du code PIN */
   if (DetectionTlbAvant && EstCeCrLectureEtatsInvalidationEtPinCode(AVANT, Etat_L))
   {
      TraiterNbEssaisCodePin(&Etat_L);
   }
   else if (DetectionTlbArriere && EstCeCrLectureEtatsInvalidationEtPinCode(ARRIERE, Etat_L))
   {
      TraiterNbEssaisCodePin(&Etat_L);
   }

   /*** Compte-rendu de lecture des donn�es agent */
   else if (DetectionTlbAvant && EstCeCrLectureDonneesAgent(AVANT, CrLecture_L))
   {
      TraiterCrLectureCarteAgent(&CrLecture_L);
   }

   else if (DetectionTlbArriere && EstCeCrLectureDonneesAgent(ARRIERE, CrLecture_L))
   {
      TraiterCrLectureCarteAgent(&CrLecture_L);
   }

   /*** Compte-rendu de saisie */
   else if (Rhm->EstCeCrSelection(CrSelection_L))
   {
      TraiterCrSelection(&CrSelection_L);
   }

   /*** Temporisation d'inaction */
   else if (Rhm->EstCeCrTempoInaction())
   {
      /* Tracer */
      TraceAgent.Tracer("*** Temporisation d'inaction expiree ***.\n");

      TraiterAbandonPriseDeService();
   }


   /*** Compte-rendu temporisation d'attente detection */
   else if (Tempo.EstCeCrTempsEcoule())
   {

      /* Si la tempo �choie c'est qu'il n'y a plus de carte pr�sente */
      CarteTLBPresente = FAUX;
      DetectionTlbAvant = FAUX;
      DetectionTlbArriere = FAUX;

      TraiterFinPriseDeService();
   }


   /*** Default */
   else
   {
      Return_L = FAUX;
   }

   // Fin de la fonction
   return Return_L;

}

/*****************************************************************************
* But de la fonction : Traiter detection carte sans contact
* --------------------
* Parametres :
*
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
unsigned int T_EtatPriseDeService::TraiterCarteTLB(void)

{
   T_APITLB_CardData DonneesCarte_L = { 0 };
   unsigned int Return_L = FAUX;

   /* Si une redetection de carte est demand�e */
   if (RedetectionCarte->Demandee == VRAI)
   {
      /* M�moriser l'�v�nement */
      RedetectionCarte->Demandee = FAUX;

      /* Arreter la temporisation */
      Tempo.ArreterTempo();

      switch (TypeSupportDetecte)
      {
      case TSC:
         memcpy(&DonneesCarte_L, &LectureTLB.TSC.Ticket.CardData, sizeof DonneesCarte_L);
         break;

      case Intertic:
         memcpy(&DonneesCarte_L, &LectureTLB.Intertic.Ticket.CardData, sizeof DonneesCarte_L);
         break;

      case CSC:
      default:
         memcpy(&DonneesCarte_L, &LectureTLB.CSC.Carte.CardData, sizeof DonneesCarte_L);
         break;
      }

      /* Afficher contr�le en cours */
      AfficherControleEnCours();

      /* Lire nombre d'essais de saisie du code PIN */
      if (DetectionTlbAvant)
      {
         LireEtatsInvalidationEtPinCode(AVANT);
      }
      else if (DetectionTlbArriere)
      {
         LireEtatsInvalidationEtPinCode(ARRIERE);
      }

      /* La carte est identique, l'evenement de detection carte est traite */
      Return_L = VRAI;
   }

   /* Fin de la fonction */
   return Return_L;
}


/*****************************************************************************
* But de la fonction :
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
unsigned int T_EtatPriseDeService::TraiterOuverturePorteAvant(void)
{

   AEteOuvert_avant = true;
   /* evt memorise donc traite en differe */
   return FAUX;

}

unsigned int T_EtatPriseDeService::TraiterOuverturePorteArriere1(void)
{
   /* evt memorise donc traite en differe */
   AEteOuvert_arriere = true;
   return FAUX;

}

/*****************************************************************************
* But de la fonction :
* --------------------
* Parametres :
* ------------
* entree : aucun
*
* sortie : aucun
*
* return : aucun
*
* Points particuliers :
*
*****************************************************************************/
unsigned int T_EtatPriseDeService::TraiterDeverrouillageCremonePorteAvant(void)
{

   AEteOuvert_avant = true;

   /* evt memorise donc traite en differe */
   return FAUX;
}

