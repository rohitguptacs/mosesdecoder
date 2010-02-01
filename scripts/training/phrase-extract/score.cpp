// $Id$
// vim:tabstop=2

/***********************************************************************
  Moses - factored phrase-based language decoder
  Copyright (C) 2009 University of Edinburgh

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ***********************************************************************/

#include <sstream>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <cstring>
#include "tables-core.h"

using namespace std;

#define LINE_MAX_LENGTH 100000

// data structure for a single phrase pair
class PhraseAlignment 
{
public:
	int target, source;
	string targetLabel, sourceLabel;
	float count;
	vector< vector<size_t> > alignedToT;
	vector< vector<size_t> > alignedToS;
  
	void create( char*, int );
	void addToCount( char* );
	void clear();
	bool equals( const PhraseAlignment& );
	bool match( const PhraseAlignment& );
};

Vocabulary vcbT;
Vocabulary vcbS;

class LexicalTable 
{
public:
  map< WORD_ID, map< WORD_ID, double > > ltable;
  void load( char[] );
  double permissiveLookup( WORD_ID wordS, WORD_ID wordT ) 
	{
		// cout << endl << vcbS.getWord( wordS ) << "-" << vcbT.getWord( wordT ) << ":";
		if (ltable.find( wordS ) == ltable.end()) return 1.0;
		if (ltable[ wordS ].find( wordT ) == ltable[ wordS ].end()) return 1.0;
		// cout << ltable[ wordS ][ wordT ];
		return ltable[ wordS ][ wordT ];
  }
};

vector<string> tokenize( const char [] );

void computeCountOfCounts( char* fileNameExtract );
void processPhrasePairs( vector< PhraseAlignment > & );
PhraseAlignment* findBestAlignment( vector< PhraseAlignment* > & );
void outputPhrasePair( vector< PhraseAlignment * > &, float );
double computeLexicalTranslation( PHRASE &, PHRASE &, PhraseAlignment * );

ofstream phraseTableFile;
ofstream wordAlignmentFile;

LexicalTable lexTable;
PhraseTable phraseTableT;
PhraseTable phraseTableS;
bool inverseFlag = false;
bool hierarchicalFlag = false;
bool newAlignmentFormatFlag = true;
bool wordAlignmentFlag = false;
bool onlyDirectFlag = false;
bool goodTuringFlag = false;
#define GT_MAX 10
bool logProbFlag = false;
int negLogProb = 1;
bool lexFlag = true;
int firstWord = 0; // in hierarchical models the first word is the label
int firstWordOutput = 0; // ... which in onlyDirect will be separated
int countOfCounts[GT_MAX+1];
float discountFactor[GT_MAX+1];

int main(int argc, char* argv[]) 
{
	cerr << "Score v2.0 written by Philipp Koehn\n"
	     << "scoring methods for extracted rules\n";
	time_t starttime = time(NULL);

	if (argc < 4) {
		cerr << "syntax: score extract lex phrase-table [--Inverse] [--Hierarchical] [--OnlyDirect] [--LogProb] [--NegLogProb] [--NoLex] [--GoodTuring] [--WordAlignment file]\n";
		exit(1);
	}
	char* fileNameExtract = argv[1];
	char* fileNameLex = argv[2];
	char* fileNamePhraseTable = argv[3];
	char* fileNameWordAlignment;

	for(int i=4;i<argc;i++) {
		if (strcmp(argv[i],"inverse") == 0 || strcmp(argv[i],"--Inverse") == 0) {
			inverseFlag = true;
			cerr << "using inverse mode\n";
		}
		else if (strcmp(argv[i],"--Hierarchical") == 0) {
			hierarchicalFlag = true;
			firstWord = 1;       // phrase starts with word 1 (word 0 is label)
			firstWordOutput = 0; // but still output label together with phrase
			cerr << "processing hierarchical rules\n";
		}
		else if (strcmp(argv[i],"--OnlyDirect") == 0) {
			onlyDirectFlag = true;
			firstWordOutput = 1; // output label separately, so it's good to go
			cerr << "outputing in correct phrase table format (no merging with inverse)\n";
		}
		else if (strcmp(argv[i],"--NewAlignmentFormat") == 0) {
			newAlignmentFormatFlag = true;
		}
		else if (strcmp(argv[i],"--WordAlignment") == 0) {
			wordAlignmentFlag = true;
			fileNameWordAlignment = argv[++i];
			cerr << "outputing word alignment in file " << fileNameWordAlignment << endl;
		}
		else if (strcmp(argv[i],"--NoLex") == 0) {
			lexFlag = false;
			cerr << "not computing lexical translation score\n";
		}
		else if (strcmp(argv[i],"--GoodTuring") == 0) {
			goodTuringFlag = true;
			cerr << "using Good Turing discounting\n";
		}
		else if (strcmp(argv[i],"--LogProb") == 0) {
			logProbFlag = true;
			cerr << "using log-probabilities\n";
		}
		else if (strcmp(argv[i],"--NegLogProb") == 0) {
			logProbFlag = true;
			negLogProb = -1;
			cerr << "using negative log-probabilities\n";
		}
		else {
			cerr << "ERROR: unknown option " << argv[i] << endl;
			exit(1);
		}
	}

	// lexical translation table
	if (lexFlag)
		lexTable.load( fileNameLex );
  
	// compute count of counts for Good Turing discounting
	if (goodTuringFlag)
		computeCountOfCounts( fileNameExtract );

	// sorted phrase extraction file
	ifstream extractFile;
	extractFile.open(fileNameExtract);
	if (extractFile.fail()) {
		cerr << "ERROR: could not open extract file " << fileNameExtract << endl;
		exit(1);
	}
	istream &extractFileP = extractFile;

	// output file: phrase translation table
	phraseTableFile.open(fileNamePhraseTable);
	if (phraseTableFile.fail()) 
	{
		cerr << "ERROR: could not open file phrase table file " 
		     << fileNamePhraseTable << endl;
		exit(1);
	}

	// output word alignment file
	if (! inverseFlag && wordAlignmentFlag)
	{
		wordAlignmentFile.open(fileNameWordAlignment);
		if (wordAlignmentFile.fail())
		{
			cerr << "ERROR: could not open word alignment file "
			     << fileNameWordAlignment << endl;
			exit(1);
		}
	}
  
  // loop through all extracted phrase translations
  int lastSource = -1;
  vector< PhraseAlignment > phrasePairsWithSameF;
  int i=0;
	char line[LINE_MAX_LENGTH],lastLine[LINE_MAX_LENGTH];
	lastLine[0] = '\0';
	PhraseAlignment *lastPhrasePair = NULL;
  while(true) {
    if (extractFileP.eof()) break;
    if (++i % 100000 == 0) cerr << "." << flush;
    SAFE_GETLINE((extractFileP), line, LINE_MAX_LENGTH, '\n');
    if (extractFileP.eof())	break;
		
		// identical to last line? just add count
		if (lastSource > 0 && strcmp(line,lastLine) == 0)
		{
			lastPhrasePair->addToCount( line );
			continue;			
		}
		strcpy( lastLine, line );

		// create new phrase pair
		PhraseAlignment phrasePair;
		phrasePair.create( line, i );
		
		// only differs in count? just add count
		if (lastPhrasePair != NULL && lastPhrasePair->equals( phrasePair ))
		{
			lastPhrasePair->count += phrasePair.count;
			phrasePair.clear();
			continue;
		}
		
		// if new source phrase, process last batch
		if (lastSource >= 0 && lastSource != phrasePair.source) {
			processPhrasePairs( phrasePairsWithSameF );
			for(int j=0;j<phrasePairsWithSameF.size();j++)
				phrasePairsWithSameF[j].clear();
			phrasePairsWithSameF.clear();
			phraseTableT.clear();
			phraseTableS.clear();
			// process line again, since phrase tables flushed
			phrasePair.clear();
			phrasePair.create( line, i ); 
		}
		
		// add phrase pairs to list, it's now the last one
		lastSource = phrasePair.source;
		lastPhrasePair = &phrasePair;
		phrasePairsWithSameF.push_back( phrasePair );
		lastPhrasePair = &phrasePairsWithSameF[phrasePairsWithSameF.size()-1];
	}
	processPhrasePairs( phrasePairsWithSameF );
	phraseTableFile.close();
	if (! inverseFlag && wordAlignmentFlag)
		wordAlignmentFile.close();
}

void computeCountOfCounts( char* fileNameExtract )
{
	cerr << "computing counts of counts";
	for(int i=1;i<=GT_MAX;i++) countOfCounts[i] = 0;

	ifstream extractFile;
	extractFile.open( fileNameExtract );
	if (extractFile.fail()) {
		cerr << "ERROR: could not open extract file " << fileNameExtract << endl;
		exit(1);
	}
	istream &extractFileP = extractFile;

	// loop through all extracted phrase translations
	int i=0;
	char line[LINE_MAX_LENGTH],lastLine[LINE_MAX_LENGTH];
	lastLine[0] = '\0';
	PhraseAlignment *lastPhrasePair = NULL;
	while(true) {
		if (extractFileP.eof()) break;
		if (++i % 100000 == 0) cerr << "." << flush;
		SAFE_GETLINE((extractFileP), line, LINE_MAX_LENGTH, '\n');
		if (extractFileP.eof())	break;
		
		// identical to last line? just add count
		if (strcmp(line,lastLine) == 0)
		{
			lastPhrasePair->addToCount( line );
			continue;			
		}
		strcpy( lastLine, line );

		// create new phrase pair
		PhraseAlignment *phrasePair = new PhraseAlignment();
		phrasePair->create( line, i );
		
		if (i == 1)
		{
			lastPhrasePair = phrasePair;
			continue;
		}

		// only differs in count? just add count
		if (lastPhrasePair->match( *phrasePair ))
		{
			lastPhrasePair->count += phrasePair->count;
			phrasePair->clear();
			delete(phrasePair);
			continue;
		}

		// periodically house cleaning
		if (phrasePair->source != lastPhrasePair->source)
		{
			phraseTableT.clear(); // these would get too big
			phraseTableS.clear(); // these would get too big
			// process line again, since phrase tables flushed
			phrasePair->clear();
			phrasePair->create( line, i ); 
		}

		int count = lastPhrasePair->count + 0.99999;
		if(count <= GT_MAX)
			countOfCounts[ count ]++;
		lastPhrasePair->clear();
		delete( lastPhrasePair );
		lastPhrasePair = phrasePair;
	}

	discountFactor[0] = 0.01; // floor
	cerr << "\n";
	for(int i=1;i<GT_MAX;i++)
	{
		discountFactor[i] = ((float)i+1)/(float)i*(((float)countOfCounts[i+1]+0.1) / ((float)countOfCounts[i]+0.1));
		cerr << "count " << i << ": " << countOfCounts[ i ] << ", discount factor: " << discountFactor[i];
		// some smoothing...
		if (discountFactor[i]>1) 
			discountFactor[i] = 1;
		if (discountFactor[i]<discountFactor[i-1])
			discountFactor[i] = discountFactor[i-1];
		cerr << " -> " << discountFactor[i]*i << endl;
	}
}

bool isNonTerminal( string &word ) 
{
	return (word.length()>=3 &&
		word.substr(0,1).compare("[") == 0 && 
		word.substr(word.length()-1,1).compare("]") == 0);
}
	
void processPhrasePairs( vector< PhraseAlignment > &phrasePair ) {
  if (phrasePair.size() == 0) return;

	// group phrase pairs based on alignments that matter
	// (i.e. that re-arrange non-terminals)
	vector< vector< PhraseAlignment * > > phrasePairGroup;
	float totalSource = 0;
	
	// loop through phrase pairs
	for(size_t i=0; i<phrasePair.size(); i++)
	{
		// add to total count
		totalSource += phrasePair[i].count;

		bool matched = false;
		// check for matches
		for(size_t g=0; g<phrasePairGroup.size(); g++)
		{
			vector< PhraseAlignment* > &group = phrasePairGroup[g];
			// matched? place into same group
			if ( group[0]->match( phrasePair[i] ))
			{
				group.push_back( &phrasePair[i] );
				matched = true;
			}
		}
		// not matched? create new group
		if (! matched) 
		{
			vector< PhraseAlignment* > newGroup;
			newGroup.push_back( &phrasePair[i] );
			phrasePairGroup.push_back( newGroup );
		}
	}
	
	for(size_t g=0; g<phrasePairGroup.size(); g++)
	{
		vector< PhraseAlignment* > &group = phrasePairGroup[g];
		outputPhrasePair( group, totalSource );
	}
}

PhraseAlignment* findBestAlignment( vector< PhraseAlignment* > &phrasePair ) 
{
	float bestAlignmentCount = -1;
	PhraseAlignment* bestAlignment;

	for(int i=0;i<phrasePair.size();i++) 
	{
		if (phrasePair[i]->count > bestAlignmentCount)
		{
			bestAlignmentCount = phrasePair[i]->count;
			bestAlignment = phrasePair[i];
		}
	}
	
	return bestAlignment;
}

void outputPhrasePair( vector< PhraseAlignment* > &phrasePair, float totalCount ) 
{
  if (phrasePair.size() == 0) return;

	PhraseAlignment *bestAlignment = findBestAlignment( phrasePair );

	// compute count
	float count = 0;
	for(size_t i=0;i<phrasePair.size();i++)
	{
		count += phrasePair[i]->count;
	}

	PHRASE phraseS = phraseTableS.getPhrase( phrasePair[0]->source );
	PHRASE phraseT = phraseTableT.getPhrase( phrasePair[0]->target );

	// labels (if hierarchical)

	if (hierarchicalFlag && onlyDirectFlag) 
	{
		if (! inverseFlag)
			phraseTableFile << vcbS.getWord( phraseS[0] ) << " ";
		phraseTableFile << vcbT.getWord( phraseT[0] ) << " ";
		if (inverseFlag)
			phraseTableFile << vcbS.getWord( phraseS[0] ) << " ";
		phraseTableFile << "||| ";
	}

	// source phrase (unless inverse)
	if (! inverseFlag) 
	{
		for(int j=firstWordOutput;j<phraseS.size();j++)
		{
			phraseTableFile << vcbS.getWord( phraseS[j] );
			phraseTableFile << " ";
		}
		phraseTableFile << "||| ";
	}
	
	// target phrase
	for(int j=firstWordOutput;j<phraseT.size();j++)
	{
		phraseTableFile << vcbT.getWord( phraseT[j] );
		phraseTableFile << " ";
	}
	phraseTableFile << "||| ";
	
	// source phrase (if inverse)
	if (inverseFlag) 
	{
		for(int j=firstWordOutput;j<phraseS.size();j++)
		{
			phraseTableFile << vcbS.getWord( phraseS[j] );
			phraseTableFile << " ";
		}
		phraseTableFile << "||| ";
	}
	
	// alignment info for non-terminals
	if (! inverseFlag && hierarchicalFlag) 
	{
		if (newAlignmentFormatFlag)
		{
			for(int j = 0; j < phraseT.size() - 1; j++)
			{
				if (isNonTerminal(vcbT.getWord( phraseT[j] )))
				{
					int sourcePos = bestAlignment->alignedToT[ j ][ 0 ];
					phraseTableFile << sourcePos << "-" << j << " ";
				}
			}
		}
		else
		{
			map< int, int > NTalignment;
			int nt = 0;
			// find positions of source non-terminals
			for(int j=0 ; j < phraseS.size() - 1; j++)
			{
				if (isNonTerminal(vcbS.getWord( phraseS[j] )))
				{
					NTalignment[j] = nt++;
				}
			}
			// match with target non-terminals
			nt=0;
			for(int j = 0; j < phraseT.size() - 1;j++)
			{
				if (isNonTerminal(vcbT.getWord( phraseT[j] )))
				{
					int sourcePos = bestAlignment->alignedToT[ j-1 ][ 0 ];
					phraseTableFile << NTalignment[ sourcePos ] << "-" << nt++ << " ";
				}
			}
		}
		phraseTableFile << "||| ";
	}

	// phrase translation probability
	if (goodTuringFlag && count<GT_MAX)
		count *= discountFactor[(int)(count+0.99999)];
	double condScore = count / totalCount;	
	phraseTableFile << ( logProbFlag ? negLogProb*log(condScore) : condScore );
	
	// lexical translation probability
	if (lexFlag)
	{
		double lexScore = computeLexicalTranslation( phraseS, phraseT, bestAlignment);
		phraseTableFile << " " << ( logProbFlag ? negLogProb*log(lexScore) : lexScore );
	}

	phraseTableFile << " ||| " << totalCount;
	phraseTableFile << endl;

	// optional output of word alignments
	if (! inverseFlag && wordAlignmentFlag)
	{
		// LHS non-terminals, if hierarchical
		if (hierarchicalFlag)
		{
			wordAlignmentFile << vcbS.getWord( phraseS[0] ) << " ";
			wordAlignmentFile << vcbT.getWord( phraseT[0] ) << " ";
			wordAlignmentFile << "||| ";
		}

		// source phrase
		for(int j=firstWord;j<phraseS.size();j++)
		{
			wordAlignmentFile << vcbS.getWord( phraseS[j] ) << " ";
		}
		wordAlignmentFile << "||| ";
	
		// target phrase
		for(int j=firstWord;j<phraseT.size();j++)
		{
			wordAlignmentFile << vcbT.getWord( phraseT[j] ) << " ";
		}
		wordAlignmentFile << "|||";

		// alignment
		for(int j=firstWord;j<phraseT.size();j++)
		{
			vector< size_t > &aligned = bestAlignment->alignedToT[ j-firstWord ];
			for(int i=0; i<aligned.size(); i++)
			{
				wordAlignmentFile << " " << aligned[i] << "-" << (j-firstWord);
			}
		}
		wordAlignmentFile << endl;
	}
}

double computeLexicalTranslation( PHRASE &phraseS, PHRASE &phraseT, PhraseAlignment *alignment ) {
	// lexical translation probability
	double lexScore = 1.0;
	int null = vcbS.getWordID("NULL");
	// all target words have to be explained
	for(int ti=firstWord;ti<phraseT.size();ti++) { 
		if (alignment->alignedToT[ ti-firstWord ].size() == 0)
		{ // explain unaligned word by NULL
			lexScore *= lexTable.permissiveLookup( null, phraseT[ ti ] ); 
		}
		else 
		{ // go through all the aligned words to compute average
			double thisWordScore = 0;
			for(int j=0;j<alignment->alignedToT[ ti-firstWord ].size();j++) {
				thisWordScore += lexTable.permissiveLookup( phraseS[alignment->alignedToT[ ti-firstWord ][ j ]+firstWord ], phraseT[ ti ] );
			}
			lexScore *= thisWordScore / (double)alignment->alignedToT[ ti-firstWord ].size();
		}
	}
	return lexScore;
}

void PhraseAlignment::addToCount( char line[] ) 
{
	vector< string > token = tokenize( line );
	int item = 0;
	for (int j=0; j<token.size(); j++) 
	{
		if (token[j] == "|||") item++;
		if (item == 4)
		{
			float addCount;
			sscanf(token[j].c_str(), "%f", &addCount);
			count += addCount;
		}
	}
	if (item < 4) // no specified counts -> counts as one
		count += 1.0;
}

// read in a phrase pair and store it
void PhraseAlignment::create( char line[], int lineID ) 
{
	//cerr << "processing " << line;
	vector< string > token = tokenize( line );
	int item = 1;
	PHRASE phraseS, phraseT;
	for (int j=0; j<token.size(); j++) 
	{
		if (token[j] == "|||") item++;
		else if (item == 1) // source phrase
		{
			phraseS.push_back( vcbS.storeIfNew( token[j] ) );
		}
		
		else if (item == 2) // target phrase
		{
			phraseT.push_back( vcbT.storeIfNew( token[j] ) );
		}
		
		else if (item == 3) // alignment
		{
			int s,t;
			sscanf(token[j].c_str(), "%d-%d", &s, &t);
			if (t >= phraseT.size() || s >= phraseS.size()) 
			{ 
				cerr << "WARNING: phrase pair " << lineID 
						 << " has alignment point (" << s << ", " << t 
						 << ") out of bounds (" << phraseS.size() << ", " << phraseT.size() << ")\n";
			}
			else 
			{
				// first alignment point? -> initialize
				if (alignedToT.size() == 0) 
				{
					vector< size_t > dummy;
					for(int i=0;i<phraseT.size();i++)
						alignedToT.push_back( dummy );
					for(int i=0;i<phraseS.size();i++)
						alignedToS.push_back( dummy );
					source = phraseTableS.storeIfNew( phraseS );
					target = phraseTableT.storeIfNew( phraseT );
				}
				// add alignment point
				alignedToT[t].push_back( s );
				alignedToS[s].push_back( t );
			}
		}
		else if (item == 4) // count
		{
			sscanf(token[j].c_str(), "%f", &count);
		}
	}
	if (item == 3)
	{
		count = 1.0;
	}
	if (item < 3 || item > 4)
	{
		cerr << "ERROR: faulty line " << lineID << ": " << line << endl;
	}
}

void PhraseAlignment::clear() {
  for(int i=0;i<alignedToT.size();i++)
 		alignedToT[i].clear();
  for(int i=0;i<alignedToS.size();i++)
		alignedToS[i].clear();
  alignedToT.clear();
  alignedToS.clear();
}

// check if two word alignments between a phrase pair are the same
bool PhraseAlignment::equals( const PhraseAlignment& other ) 
{
	if (this == &other) return true;
	if (other.target != target) return false;
	if (other.source != source) return false;
	PHRASE phraseT = phraseTableT.getPhrase( target );
	PHRASE phraseS = phraseTableS.getPhrase( source );
	for(int i=0;i<phraseT.size();i++) 
	{
		if (alignedToT[i].size() != other.alignedToT[i].size()) 
			return false;
		for(int j=0; j<alignedToT[i].size(); j++) 
		{
			if (alignedToT[i][j] != other.alignedToT[i][j]) 
				return false;
		}
	}
	for(int i=0;i<phraseS.size();i++) 
	{
		if (alignedToS[i].size() != other.alignedToS[i].size()) 
			return false;
		for(int j=0; j<alignedToS[i].size(); j++) 
		{
			if (alignedToS[i][j] != other.alignedToS[i][j]) 
				return false;
		}
	}
	return true;
}

// check if two word alignments between a phrase pairs "match"
// i.e. they do not differ in the alignment of non-termimals
bool PhraseAlignment::match( const PhraseAlignment& other )
{
	if (this == &other) return true;
	if (other.target != target) return false;
	if (other.source != source) return false;
	if (!hierarchicalFlag) return true;

	PHRASE phraseT = phraseTableT.getPhrase( target );
	// loop over all words (note: 0 = left hand side of rule)
	for(int i=1;i<phraseT.size();i++)
	{
		if (isNonTerminal( vcbT.getWord( phraseT[i] ) ))
		{
			if (alignedToT[i-1].size() != 1 ||
			    other.alignedToT[i-1].size() != 1 ||
		    	    alignedToT[i-1][0] != other.alignedToT[i-1][0])
				return false;
		}
	}
	return true;
}


void LexicalTable::load( char *fileName ) 
{
  cerr << "Loading lexical translation table from " << fileName;
  ifstream inFile;
  inFile.open(fileName);
  if (inFile.fail()) 
	{
    cerr << " - ERROR: could not open file\n";
    exit(1);
  }
  istream *inFileP = &inFile;

  char line[LINE_MAX_LENGTH];

  int i=0;
  while(true) 
	{
    i++;
    if (i%100000 == 0) cerr << "." << flush;
    SAFE_GETLINE((*inFileP), line, LINE_MAX_LENGTH, '\n');
    if (inFileP->eof()) break;

    vector<string> token = tokenize( line );
    if (token.size() != 3) 
		{
      cerr << "line " << i << " in " << fileName 
			     << " has wrong number of tokens, skipping:\n"
			     << token.size() << " " << token[0] << " " << line << endl;
      continue;
    }
    
    double prob = atof( token[2].c_str() );
    WORD_ID wordT = vcbT.storeIfNew( token[0] );
    WORD_ID wordS = vcbS.storeIfNew( token[1] );
    ltable[ wordS ][ wordT ] = prob;
  }
  cerr << endl;
}
