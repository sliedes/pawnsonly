from math import log

OVERHEAD_BITS = 2

def binom(n,k):
    a = 1
    for m in range(1,k+1):
        a = a*(n-k+m)/m
    return a


def rank_combination(cs, k):
    a = 0;
    for i in range(k):
        a += binom(cs[i], i+1)
    return a


def solve_size(total, bpe):
    assert bpe%8 == 0
    bits = bpe/8

    # total / 2^(bits-OVERHEAD_BITS) = 2^bits
    # => 2^bits * 2^(bits-OVERHEAD_BITS) = total
    # => 2^(2*bits-OVERHEAD_BITS) = total
    # => 2*bits-OVERHEAD_BITS = log(total)
    # => bits = (log(total)-OVERHEAD_BITS)/2

def count_boards(N):
    total = 0
    itotal = 0
    assert N >= 4
    NUM_ISQ = N*(N-2)
    print 'Possible %dx%x boards with a+b pawns:\n' % (N,N)
    FORMAT='%-5s  %-11f  %-18d  %-8.5f  %-18d'
    FORMAT_S='%-5s  %-11s  %-18s  %-8s  %-18s'
    HEADER = FORMAT_S % ('a+b', 'stupid_bits', 'stupid_count', 'int_bits', 'int_count')
    print HEADER
    print '-'*len(HEADER)
    for a in range(1,N+1):
        for b in range(1,N+1):
            count = binom(NUM_ISQ, a) * binom(NUM_ISQ, b)
            total += count
            icount = binom(NUM_ISQ, a) * binom(NUM_ISQ-a, b)
            itotal += icount
            print FORMAT % ('%d+%d' % (a,b),
                            log(count)/log(2), count,
                            log(icount)/log(2), icount)
    print '-'*len(HEADER)
    print FORMAT % ('TOTAL', log(total)/log(2), total, log(icount)/log(2), itotal)
    print
    print 'TP table sizes for n bits/element:'
    print
    print '                   Stupid                   |            Intelligent            '
    print 'bits/e   size                   size (GiB)  |  size                   size (GiB)'
    print '--------------------------------------------+-----------------------------------'
    FORMAT = '%-7d  %-21d  %-11.1f |  %-21d  %-11.1f'
    for be in [1,2,3,4,5,6,7]:
        BITS = be*8-OVERHEAD_BITS
        # total/size < 2^bits
        # => size = total/2^bits
        s = total/2.0**BITS
        si = itotal/2.0**BITS
        print FORMAT % (BITS, s, s*be/2**30, si, si*be/2**30)

        


def main():
    count_boards(8)


if __name__ == '__main__':
    main()
