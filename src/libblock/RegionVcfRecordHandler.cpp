// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Copyright (c) 2009-2012 Illumina, Inc.
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
//

/// \file
///
/// \author Chris Saunders
///


#include "parse_util.hh"
#include "RegionVcfRecordHandler.hh"

#include <cstdlib>

#include <iostream>



namespace {
std::ostream& log_os(std::cerr);
}



RegionVcfOptions::
RegionVcfOptions() :
    outfp(std::cout),
    isExcludeOffTarget(false),
    isIncludeVariants(false)
{}



void
RegionVcfRecordHandler::
process_line(const istream_line_splitter& vparse) {
    const unsigned nw(vparse.n_word());

    if (nw != (VCFID::SAMPLE+1)) {
        log_os << "ERROR: unexpected number of fields in vcf record:\n";
        vparse.dump(log_os);
        exit(EXIT_FAILURE);
    }

    // 1. check if record region is in a target region at all,
    //    if not, then write out the record:
    //
    if (! is_record_in_region(vparse)) {
        if (is_write_off_region_record(vparse)) {
            vparse.write_line(_opt.outfp);
        }
        return;
    }

    // 2. This record interacts with the target region. To process
    // it, an iterator provides begin,end ranges on successive calls
    // for the record region, untill it's completely classified into
    // intersetions with the target:
    //
    VcfRecord vcfr(vparse);

    bool is_in_region;
    unsigned end;
    while (get_next_record_region_interval(is_in_region,end)) {
        VcfRecord vcfr2(vcfr);
        process_block(is_in_region,end,vcfr2);
        vcfr.SetPos(end+1);
        vcfr.SetRef(_scp.get_char(vcfr.GetChrom().c_str(),static_cast<int>(end+1)));
    }
    process_block(is_in_region,end,vcfr);
}



bool
RegionVcfRecordHandler::
is_record_in_region(const istream_line_splitter& vparse) {
    // determine if chromosome is new:
    if (_last_chrom.empty() || (0 != strcmp(_last_chrom.c_str(),vparse.word[0]))) {
        _last_chrom=vparse.word[VCFID::CHROM];
        const region_util::region_t::const_iterator i(_opt.regions.find(_last_chrom));
        _is_skip_chrom=((i==_opt.regions.end()) || (i->second.empty()));

        // setup region iterators:
        if (! _is_skip_chrom) {
            _rhead=i->second.begin();
            _rend=i->second.end();
        }
    }

    if (! _is_skip_chrom) {
        // get start pos:
        get_vcf_end_record_range(vparse.word, _begin_pos, _end_pos);

        while (_rhead != _rend) {
            if (_begin_pos>_rhead->second) {
                _rhead++;
            } else {
                return(_end_pos>_rhead->first);
            }
        }
        _is_skip_chrom=true;
    }
    return false;
}



bool
RegionVcfRecordHandler::
is_write_off_region_record(const istream_line_splitter& vparse) const {
    if (! _opt.isExcludeOffTarget) return true;

    if (_opt.isIncludeVariants) {
        if (is_variant_record(vparse.word, _gtparse)) return true;
    }
    return false;
}


/// \brief do we output this record, assuming it is off-region
bool
RegionVcfRecordHandler::
is_write_off_region_record(const VcfRecord& vcfr) const {
    if (! _opt.isExcludeOffTarget) return true;

    if (_opt.isIncludeVariants) {
        if (vcfr.IsStrictVariant()) return true;
    }
    return false;
}



bool
RegionVcfRecordHandler::
get_next_record_region_interval(bool& is_in_region,
                                unsigned& end) {

    assert(_begin_pos <= _end_pos);

    if (_begin_pos > _rhead->second) _rhead++;
    if (_rhead == _rend) { // no haploid regions left
        is_in_region = false;
        end = _end_pos;
        return false;
    }

    // our next interval:
    if (_begin_pos <= _rhead->first) {
        end = std::min( _end_pos,_rhead->first);
    } else {
        end = std::min( _end_pos, _rhead->second);
    }

    // test for intercept:
    is_in_region = ((_begin_pos <= _rhead->second) && (end > _rhead->first));

    _begin_pos = end+1;
    return (_begin_pos<=_end_pos);
}

