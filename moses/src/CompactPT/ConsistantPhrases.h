#ifndef moses_ConsistantPhrases_h
#define moses_ConsistantPhrases_h

#include <set>

namespace Moses {

class ConsistantPhrases {
  public:
    struct Phrase {
      int i, j, m, n;
      Phrase(int i_, int m_, int j_, int n_) : i(i_), j(j_), m(m_), n(n_) {
        //std::cerr << "Creating phrase: (" << i << "," << m << ","  << j << ","  << n << ")" << std::endl;
      }
    };
    
    struct PhraseSorter {
      bool operator()(Phrase a, Phrase b) {
        if(a.n > b.n)
          return true;
        if(a.n == b.n && a.j < b.j)
          return true;
        if(a.n == b.n && a.j == b.j && a.m > b.m)
          return true;
        if(a.n == b.n && a.j == b.j && a.m == b.m && a.i < b.i)
          return true;
        
        return false;
      }
    };
    
  private:
    typedef std::set<Phrase, PhraseSorter> PhraseQueue;
    PhraseQueue m_phraseQueue;
    
  public:
    
    template <class It>
    ConsistantPhrases(int mmax, int nmax, It begin, It end) {
      for(int i = 0; i < mmax; i++) {
        for(int m = 1; m <= mmax-i; m++) {
          for(int j = 0; j < nmax; j++) {
            for(int n = 1; n <= nmax-j; n++) {
              bool consistant = true;
              for(It it = begin; it != end; it++) {
                int ip = it->first;
                int jp = it->second;
                if((i <= ip && ip < i+m) != (j <= jp && jp < j+n)) {
                  consistant = false;
                  break;
                }
              }
              if(consistant)
                m_phraseQueue.insert(Phrase(i, m, j, n));
            } 
          }  
        } 
      }
      m_phraseQueue.erase(Phrase(0, mmax, 0, nmax));
    }
    
    size_t size() {
      return m_phraseQueue.size();
    }
    
    Phrase pop() {
      if(m_phraseQueue.size()) {
        Phrase p = *m_phraseQueue.begin();
        m_phraseQueue.erase(m_phraseQueue.begin());
        return p;
      }
      return Phrase(0,0,0,0);
    }
    
    void removeOverlap(Phrase p) {
      PhraseQueue ok;
      for(PhraseQueue::iterator it = m_phraseQueue.begin(); it != m_phraseQueue.end(); it++) {
        Phrase pp = *it;
        if(!((p.i <= pp.i && pp.i < p.i + p.m) || (pp.i <= p.i && p.i < pp.i + pp.m) ||
          (p.j <= pp.j && pp.j < p.j + p.n) || (pp.j <= p.j && p.j < pp.j + pp.n)))
          ok.insert(pp);
      }
      m_phraseQueue = ok;
    }
  
};

}

#endif