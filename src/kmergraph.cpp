#include <iostream>
#include <sstream>
#include <cstring>
#include <fstream>
#include <cassert>
#include <vector>
#include <limits>
//#include <algorithm>
#include "utils.h"
#include "kmernode.h"
#include "kmergraph.h"

#define assert_msg(x) !(std::cerr << "Assertion failed: " << x << std::endl)

using namespace std;

KmerGraph::KmerGraph()
{
    nodes.reserve(60000);
    next_id = 0;
    num_reads = 0;
    shortest_path_length = 0;    
    k = 0; // nb the kmer size is determined by the first non-null node added
    p = 1;
}

KmerGraph::~KmerGraph()
{
    clear();
}

void KmerGraph::clear()
{
    for (auto c: nodes)
    {
        delete c;
    }
    nodes.clear();
    assert(nodes.size() == 0);
    next_id = 0;
    num_reads = 0;
    shortest_path_length = 0;
    k = 0;
    p = 1;
}

KmerNode* KmerGraph::add_node (const Path& p)
{
    KmerNode *n;
    n = new KmerNode(next_id, p);
    pointer_values_equal<KmerNode> eq = { n };
    vector<KmerNode*>::iterator it = find_if(nodes.begin(), nodes.end(), eq);
    if ( it == nodes.end() )
    {
	nodes.push_back(n);
	//cout << "added node " << *n;
	assert(k==0 or p.length()==0 or p.length()==k);
	if (k == 0 and p.length() > 0)
	{
	    k = p.length();
	}  
	next_id++;
    } else {
	//cout << "node " << *n << " was duplicate" << endl;
	delete n;
	n = *it;
    }

    return n;
}

KmerNode* KmerGraph::add_node_with_kh (const Path& p, const uint64_t& kh, const uint8_t& num)
{
    KmerNode *n = add_node(p);
    n->khash = kh;
    n->num_AT = num;
    assert(n->khash < std::numeric_limits<uint64_t>::max());
    return n;
}
    
condition::condition(const Path& p): q(p) {};
bool condition::operator()(const KmerNode* kn) const { return kn->path == q; }

void KmerGraph::add_edge (const Path& from, const Path& to)
{
    assert(from < to ||assert_msg(from << " is not less than " << to) );
    if (from == to)
    {
	return;
    }

    vector<KmerNode*>::iterator from_it = find_if(nodes.begin(), nodes.end(), condition(from));
    vector<KmerNode*>::iterator to_it = find_if(nodes.begin(), nodes.end(), condition(to));
    assert(from_it != nodes.end() && to_it != nodes.end());

    if ( find((*from_it)->outNodes.begin(), (*from_it)->outNodes.end(), (*to_it)) == (*from_it)->outNodes.end() )
    {
        (*from_it)->outNodes.push_back(*to_it);
	(*to_it)->inNodes.push_back((*from_it));
	//cout << "added edge from " << (*from_it)->id << " to " << (*to_it)->id << endl;
    }

    return;
}

void KmerGraph::add_edge (KmerNode* from, KmerNode* to)
{
    assert(from->path < to->path ||assert_msg(from->id << " is not less than " << to->id) );

    if ( find(from->outNodes.begin(), from->outNodes.end(), to) == from->outNodes.end() )
    {
        from->outNodes.push_back(to);
        to->inNodes.push_back(from);
        //cout << "added edge from " << from->id << " to " << to->id << endl;
    }
    return;
}

void KmerGraph::check (uint num_minikmers)
{
    // should have a node for every minikmer found, plus a dummy start and end
    assert(num_minikmers == 0 or nodes.size() == num_minikmers || assert_msg("nodes.size(): " << nodes.size() << " and num minikmers: " << num_minikmers));

    // should not have any leaves, only nodes with degree 0 are start and end
    for (auto c: nodes)
    {
	assert(c->inNodes.size() > 0 or c->id == 0 || assert_msg("node" << *c << " has inNodes size " << c->inNodes.size()));
	assert(c->outNodes.size() > 0 or c->id == nodes.size() - 1 || assert_msg("node" << *c << " has outNodes size " << c->outNodes.size()));
	for (auto d: c->outNodes)
	{
	    assert(c->path < d->path || assert_msg(c->path << " is not less than " << d->path));
	    assert(c->id < d->id || assert_msg(c->id << " is not less than " << d->id));
	}
    }
    return;
}

void KmerGraph::sort_topologically()
{
    sort(nodes.begin(), nodes.end(), pCompKmerNode());
    //cout << "reallocate ids" << endl;
    for (uint i=0; i!=nodes.size(); ++i)
    {
	nodes[i]->id = i;
    }
    return;
}

float KmerGraph::prob(uint j)
{
    float ret;
    if (j==0 or j==nodes.size()-1)
    {    ret = 0; // is really undefined
    } else if (nodes[j]->covg[0]+nodes[j]->covg[1] > num_reads)
    {
	// under model assumptions this can't happen, but it inevitably will, so bodge
	ret = lognchoosek2(nodes[j]->covg[0]+nodes[j]->covg[1], nodes[j]->covg[0], nodes[j]->covg[1]) + (nodes[j]->covg[0]+nodes[j]->covg[1])*log(p/2);
        // note this may give disadvantage to repeat kmers
    } else {
        ret = lognchoosek2(num_reads, nodes[j]->covg[0], nodes[j]->covg[1]) + (nodes[j]->covg[0]+nodes[j]->covg[1])*log(p/2) + 
		(num_reads-(nodes[j]->covg[0]+nodes[j]->covg[1]))*log(1-p);
    }
    return ret;
}

void KmerGraph::discover_p(float e_rate)
{
    // default based on input parameter for e_rate
    p = 1/exp(e_rate*k);

    // if we can improve on this, do
    if (num_reads > 40) 
    {
        // collect total coverages for kmers seen more than a couple of times (there is a peak at 0 because of kmers not present which we want to avoid)
        // this is currently done with a hard threshold of covg > 4, but a variable one could be introduced, or for higher coverages, the heavy 0 tail
	// will increase estimated error rate. Alternatively, could introduce a mode based method.
	vector<uint> kmer_covgs;
	for (uint j=nodes.size()-1; j!=0; --j)
	{
	    if (nodes[j]->covg[0]+nodes[j]->covg[1]>4)
	    {
		kmer_covgs.push_back(nodes[j]->covg[0]+nodes[j]->covg[1]);
	    }
	}

	// find mean of these
	if (kmer_covgs.size() == 0)
	{
	    // default to input
	    return;
	}
	float mean = std::accumulate(kmer_covgs.begin(), kmer_covgs.end(), 0.0) / kmer_covgs.size();
	p = mean/num_reads;
	cout << now() << "Found sufficient coverage to change estimated error rate from " << e_rate << " to " << -log(p)/15 << endl;
    }
    return;
}

float KmerGraph::find_max_path(float e_rate, vector<KmerNode*>& maxpath)
{
    discover_p(e_rate);
    //p = 1/exp(e_rate*k);
    //cout << " with parameters n: " << num_reads << " and p: " << p << endl;
    //cout << "Kmer graph has " << nodes.size() << " nodes" << endl;

    // create vectors to hold the intermediate values
    vector<float> M(nodes.size(), 0); // max log prob pf paths from pos i to end of graph
    vector<int> len(nodes.size(), 0); // length of max log path from pos i to end of graph
    vector<uint> prev(nodes.size(), nodes.size()-1); // prev node along path
    float max_mean;
    int max_len;

    for (uint j=nodes.size()-1; j!=0; --j)
    {
        max_mean = numeric_limits<float>::lowest();
        max_len = 0; // tie break with longest kmer path
        for (uint i=0; i!=nodes[j-1]->outNodes.size(); ++i)
        {
            if ((nodes[j-1]->outNodes[i]->id == nodes.size()-1 and -25 > max_mean + 0.000001) or 
		(M[nodes[j-1]->outNodes[i]->id]/len[nodes[j-1]->outNodes[i]->id] > max_mean + 0.000001) or
                (max_mean - M[nodes[j-1]->outNodes[i]->id]/len[nodes[j-1]->outNodes[i]->id] <= 0.000001 and len[nodes[j-1]->outNodes[i]->id] > max_len))
            {
                M[j-1] = prob(j-1) + M[nodes[j-1]->outNodes[i]->id];
                len[j-1] = 1 + len[nodes[j-1]->outNodes[i]->id];
                prev[j-1] = nodes[j-1]->outNodes[i]->id;
		//cout << j-1 << " path: " << nodes[j-1]->path << " has prob: " << prob(j-1) << "  M: " << M[j-1] << " len: " << len[j-1] << " prev: " << prev[j-1];
		if (nodes[j-1]->outNodes[i]->id != nodes.size()-1)
		{
                    max_mean = M[nodes[j-1]->outNodes[i]->id]/len[nodes[j-1]->outNodes[i]->id];
		    max_len = len[nodes[j-1]->outNodes[i]->id];
		  //  cout << " and new max_mean: " << max_mean;
		} else {
		    max_mean = log(0.005);
		}
		//cout << endl;
            }
        }
        //cout << j-1 << " path: " << nodes[j-1]->path << "  M: " << M[j-1] << " len: " << len[j-1] << " prev: " << prev[j-1] << endl;
    }

    // extract path
    uint prev_node = prev[0];
    while (prev_node < nodes.size() - 1)
    {
        //cout << prev_node << "->";
        maxpath.push_back(nodes[prev_node]);
        prev_node = prev[prev_node];
    }
    //cout << endl;

    return M[0]/len[0];
}

void KmerGraph::save_covg_dist(const string& filepath)
{

    ofstream handle;
    handle.open(filepath);

    for (uint j=1; j!=nodes.size()-1; ++j)
    {
        handle << nodes[j]->covg[0] << "," << nodes[j]->covg[1] << "," << (unsigned)nodes[j]->num_AT << " ";
    }
    handle.close();
    return;
}

uint KmerGraph::min_path_length()
{
    if (shortest_path_length > 0)
    {
	return shortest_path_length;
    }

    vector<uint> len(nodes.size(), 0); // length of shortest path from pos i to end of graph
    for (uint j=nodes.size()-1; j!=0; --j)
    {
        for (uint i=0; i!=nodes[j-1]->outNodes.size(); ++i)
        {
	    if (len[nodes[j-1]->outNodes[i]->id] + 1 > len[j-1])
	    {
		len[j-1] = len[nodes[j-1]->outNodes[i]->id] + 1;
	    }
	}
    }
    shortest_path_length = len[0];
    return len[0];
}

void KmerGraph::save (const string& filepath)
{
    ofstream handle;
    handle.open (filepath);
    handle << "H\tVN:Z:1.0\tbn:Z:--linear --singlearr" << endl;
    for(uint i=0; i!=nodes.size(); ++i)
    {
        handle << "S\t" << nodes[i]->id << "\t" << nodes[i]->path << "\tRC:i:" << nodes[i]->covg[1] << "\t" << (unsigned)nodes[i]->num_AT << endl;// << "," << nodes[i]->covg[0] << endl;
        for (uint32_t j=0; j<nodes[i]->outNodes.size(); ++j)
        {
            handle << "L\t" << nodes[i]->id << "\t+\t" << nodes[i]->outNodes[j]->id << "\t+\t0M" << endl;
        }
    }
    handle.close();
}

void KmerGraph::load (const string& filepath)
{
    string line;
    vector<string> split_line;
    stringstream ss;
    uint32_t id, covg, from, to;
    Path p;
    KmerNode* n;

    ifstream myfile (filepath);
    if (myfile.is_open())
    {
        while ( getline (myfile,line).good() )
        {
	    if (line[0] == 'S')
            {
                split_line = split(line, "\t");
                assert(split_line.size() >= 4);
                id = stoi(split_line[1]);
		ss << split_line[2];
		ss >> p;
		ss.clear();
                //add_node(p);
                n = new KmerNode(next_id, p);
		nodes.push_back(n);
		next_id++;
		if (k == 0 and p.length() > 0)
                {
                    k = p.length();
                }
		assert(nodes.back()->id == id);
		covg = stoi(split(split_line[3], "RC:i:")[0]);
		nodes.back()->covg[0] = covg;
		if (split_line.size() >= 5)
		{
		    nodes.back()->num_AT = stoi(split_line[4]);
		}
		//covg = stoi(split(split(split_line[3], "RC:i:")[0], ",")[0]);
		//nodes.back()->covg[0] = covg;
		//covg = stoi(split(split(split_line[3], "RC:i:")[0], ",")[1]);
                //nodes.back()->covg[1] = covg;
            }
	}
        myfile.clear();
        myfile.seekg(0, myfile.beg);
        while ( getline (myfile,line).good() )
        {
            if (line[0] == 'L')
            {
                split_line = split(line, "\t");
                assert(split_line.size() >= 5);
                if (split_line[2] == split_line[4])
                {
                    from = stoi(split_line[1]);
                    to = stoi(split_line[3]);
                } else {
                    from = stoi(split_line[3]);
                    to = stoi(split_line[1]);
                }
                //add_edge(from, to);
                nodes[from]->outNodes.push_back(nodes[to]);
		nodes[to]->inNodes.push_back(nodes[from]);
            }
        }
    } else {
        cerr << "Unable to open kmergraph file " << filepath << endl;
        exit(1);
    }
    return;		
}

bool KmerGraph::operator == (const KmerGraph& y) const
{
    // false if have different numbers of nodes
    if (y.nodes.size() != nodes.size()) {//cout << "different numbers of nodes" << endl; 
        return false;}

    // false if have different nodes
    for (uint i=0; i!=nodes.size(); ++i)
    {
        // if node not equal to a node in y, then false
        pointer_values_equal<KmerNode> eq = { nodes[i] };
	vector<KmerNode*>::const_iterator found = find_if(y.nodes.begin(), y.nodes.end(), eq);
        if ( found == y.nodes.end() )
	{
            return false;
	}

	// if the node is found but has different edges, then false
	if (nodes[i]->outNodes.size() != (*found)->outNodes.size()) {return false;}
	if (nodes[i]->inNodes.size() != (*found)->inNodes.size()) {return false;}
	for (uint32_t j=0; j!=nodes[i]->outNodes.size(); ++j)
        {
            pointer_values_equal<KmerNode> eq2 = { nodes[i]->outNodes[j] };
            if ( find_if((*found)->outNodes.begin(), (*found)->outNodes.end(), eq2) == (*found)->outNodes.end() )
            {return false;}
        }
	
    }
    // otherwise is true
    return true;
}

bool pCompKmerNode::operator()(KmerNode* lhs, KmerNode* rhs) {
        return (lhs->path)<(rhs->path);
}

std::ostream& operator<< (std::ostream & out, KmerGraph const& data) {
    for (const auto c: data.nodes)
    {
        out << *c;
    }
    return out ;
}
