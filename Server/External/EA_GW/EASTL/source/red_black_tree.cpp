/*
Copyright (C) 2005,2009,2010,2012 Electronic Arts, Inc.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1.  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
2.  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
3.  Neither the name of Electronic Arts, Inc. ("EA") nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY ELECTRONIC ARTS AND ITS CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ELECTRONIC ARTS OR ITS CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



///////////////////////////////////////////////////////////////////////////////
// The tree insert and erase functions below are based on the original 
// HP STL tree functions. Use of these functions was been approved by
// EA legal on November 4, 2005 and the approval documentation is available
// from the EASTL maintainer or from the EA legal deparatment on request.
// 
// Copyright (c) 1994
// Hewlett-Packard Company
// 
// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation. Hewlett-Packard Company makes no
// representations about the suitability of this software for any
// purpose. It is provided "as is" without express or implied warranty.
///////////////////////////////////////////////////////////////////////////////




#include <EASTL/internal/config.h>
#include <EASTL/internal/red_black_tree.h>
#include <stddef.h>



namespace eastl
{
    // Forward declarations
    rbtree_node_base* RBTreeRotateLeft(rbtree_node_base* pNode, rbtree_node_base* pNodeRoot);
    rbtree_node_base* RBTreeRotateRight(rbtree_node_base* pNode, rbtree_node_base* pNodeRoot);



    /// RBTreeIncrement
    /// Returns the next item in a sorted red-black tree.
    ///
    EASTL_API rbtree_node_base* RBTreeIncrement(const rbtree_node_base* pNode)
    {
        if(pNode->mpNodeRight) 
        {
            pNode = pNode->mpNodeRight;

            while(pNode->mpNodeLeft)
                pNode = pNode->mpNodeLeft;
        }
        else 
        {
            rbtree_node_base* pNodeTemp = pNode->mpNodeParent.get_node();

            while(pNode == pNodeTemp->mpNodeRight) 
            {
                pNode = pNodeTemp;
                pNodeTemp = pNodeTemp->mpNodeParent.get_node();
            }

            if(pNode->mpNodeRight != pNodeTemp)
                pNode = pNodeTemp;
        }

        return const_cast<rbtree_node_base*>(pNode);
    }



    /// RBTreeIncrement
    /// Returns the previous item in a sorted red-black tree.
    ///
    EASTL_API rbtree_node_base* RBTreeDecrement(const rbtree_node_base* pNode)
    {
        if((pNode->mpNodeParent.get_node()->mpNodeParent.get_node() == pNode ) && ( pNode->get_color() == kRBTreeColorRed ) )
            return pNode->mpNodeRight;
        else if(pNode->mpNodeLeft)
        {
            rbtree_node_base* pNodeTemp = pNode->mpNodeLeft;

            while(pNodeTemp->mpNodeRight)
                pNodeTemp = pNodeTemp->mpNodeRight;

            return pNodeTemp;
        }

        rbtree_node_base* pNodeTemp = pNode->mpNodeParent.get_node();

        while(pNode == pNodeTemp->mpNodeLeft) 
        {
            pNode     = pNodeTemp;
            pNodeTemp = pNodeTemp->mpNodeParent.get_node();
        }

        return const_cast<rbtree_node_base*>(pNodeTemp);
    }



    /// RBTreeGetBlackCount
    /// Counts the number of black nodes in an red-black tree, from pNode down to the given bottom node.  
    /// We don't count red nodes because red-black trees don't really care about
    /// red node counts; it is black node counts that are significant in the 
    /// maintenance of a balanced tree.
    ///
    EASTL_API size_t RBTreeGetBlackCount(const rbtree_node_base* pNodeTop, const rbtree_node_base* pNodeBottom)
    {
        size_t nCount = 0;

        for(; pNodeBottom; pNodeBottom = pNodeBottom->mpNodeParent.get_node())
        {
            if(pNodeBottom->get_color() == kRBTreeColorBlack) 
                ++nCount;

            if(pNodeBottom == pNodeTop) 
                break;
        }

        return nCount;
    }


    /// RBTreeRotateLeft
    /// Does a left rotation about the given node. 
    /// If you want to understand tree rotation, any book on algorithms will
    /// discussion the topic in good detail.
    rbtree_node_base* RBTreeRotateLeft(rbtree_node_base* pNode, rbtree_node_base* pNodeRoot)
    {
        rbtree_node_base* const pNodeTemp = pNode->mpNodeRight;

        pNode->mpNodeRight = pNodeTemp->mpNodeLeft;

        if(pNodeTemp->mpNodeLeft)
            pNodeTemp->mpNodeLeft->mpNodeParent.set_node(pNode);
        pNodeTemp->mpNodeParent.set_node(pNode->mpNodeParent.get_node());
        
        if(pNode == pNodeRoot)
            pNodeRoot = pNodeTemp;
        else if(pNode == pNode->mpNodeParent.get_node()->mpNodeLeft )
            pNode->mpNodeParent.get_node()->mpNodeLeft = pNodeTemp;
        else
            pNode->mpNodeParent.get_node()->mpNodeRight = pNodeTemp;

        pNodeTemp->mpNodeLeft = pNode;
        pNode->mpNodeParent.set_node( pNodeTemp );

        return pNodeRoot;
    }



    /// RBTreeRotateRight
    /// Does a right rotation about the given node. 
    /// If you want to understand tree rotation, any book on algorithms will
    /// discussion the topic in good detail.
    rbtree_node_base* RBTreeRotateRight(rbtree_node_base* pNode, rbtree_node_base* pNodeRoot)
    {
        rbtree_node_base* const pNodeTemp = pNode->mpNodeLeft;

        pNode->mpNodeLeft = pNodeTemp->mpNodeRight;

        if(pNodeTemp->mpNodeRight)
            pNodeTemp->mpNodeRight->mpNodeParent.set_node(pNode);
        pNodeTemp->mpNodeParent.set_node( pNode->mpNodeParent.get_node() );

        if(pNode == pNodeRoot)
            pNodeRoot = pNodeTemp;
        else if(pNode == pNode->mpNodeParent.get_node()->mpNodeRight )
            pNode->mpNodeParent.get_node()->mpNodeRight = pNodeTemp;
        else
            pNode->mpNodeParent.get_node()->mpNodeLeft = pNodeTemp;

        pNodeTemp->mpNodeRight = pNode;
        pNode->mpNodeParent.set_node( pNodeTemp );

        return pNodeRoot;
    }




    /// RBTreeInsert
    /// Insert a node into the tree and rebalance the tree as a result of the 
    /// disturbance the node introduced.
    ///
    EASTL_API void RBTreeInsert(rbtree_node_base* pNode,
                                rbtree_node_base* pNodeParent, 
                                rbtree_node_base* pNodeAnchor,
                                RBTreeSide insertionSide)
    {
        // Initialize fields in new node to insert.
        pNode->mpNodeParent.set_node( pNodeParent );
        pNode->mpNodeRight  = NULL;
        pNode->mpNodeLeft   = NULL;
        pNode->set_color( kRBTreeColorRed );

        // Insert the node.
        if(insertionSide == kRBTreeSideLeft)
        {
            pNodeParent->mpNodeLeft = pNode; // Also makes (leftmost = pNode) when (pNodeParent == pNodeAnchor)

            if(pNodeParent == pNodeAnchor)
            {
                pNodeAnchor->mpNodeParent.set_node( pNode );
                pNodeAnchor->mpNodeRight = pNode;
            }
            else if(pNodeParent == pNodeAnchor->mpNodeLeft)
                pNodeAnchor->mpNodeLeft = pNode; // Maintain leftmost pointing to min node
        }
        else
        {
            pNodeParent->mpNodeRight = pNode;

            if(pNodeParent == pNodeAnchor->mpNodeRight)
                pNodeAnchor->mpNodeRight = pNode; // Maintain rightmost pointing to max node
        }

        rbtree_node_base* pRootNode = pNodeAnchor->mpNodeParent.get_node();

        // Rebalance the tree.
        while((pNode != pRootNode) && (pNode->mpNodeParent.get_node()->get_color() == kRBTreeColorRed ) )
        {
            rbtree_node_base* const pNodeParentParent = pNode->mpNodeParent.get_node()->mpNodeParent.get_node();

            if(pNode->mpNodeParent.get_node() == pNodeParentParent->mpNodeLeft) 
            {
                rbtree_node_base* const pNodeTemp = pNodeParentParent->mpNodeRight;

                if(pNodeTemp && (pNodeTemp->get_color() == kRBTreeColorRed)) 
                {
                    pNode->mpNodeParent.get_node()->set_color( kRBTreeColorBlack );
                    pNodeTemp->set_color(kRBTreeColorBlack);
                    pNodeParentParent->set_color( kRBTreeColorRed );
                    pNode = pNodeParentParent;
                }
                else 
                {
                    if(pNode == pNode->mpNodeParent.get_node()->mpNodeRight )
                    {
                        pNode = pNode->mpNodeParent.get_node();
                        pRootNode = RBTreeRotateLeft( pNode, pRootNode );
                        pNodeAnchor->mpNodeParent.set_node( pRootNode );
                    }

                    pNode->mpNodeParent.get_node()->set_color(kRBTreeColorBlack);
                    pNodeParentParent->set_color( kRBTreeColorRed );
                    pRootNode = RBTreeRotateRight(pNodeParentParent, pRootNode);
                    pNodeAnchor->mpNodeParent.set_node( pRootNode );
                }
            }
            else 
            {
                rbtree_node_base* const pNodeTemp = pNodeParentParent->mpNodeLeft;

                if(pNodeTemp && (pNodeTemp->get_color() == kRBTreeColorRed)) 
                {
                    pNode->mpNodeParent.get_node()->set_color( kRBTreeColorBlack );
                    pNodeTemp->set_color(kRBTreeColorBlack);
                    pNodeParentParent->set_color( kRBTreeColorRed );
                    pNode = pNodeParentParent;
                }
                else 
                {
                    if(pNode == pNode->mpNodeParent.get_node()->mpNodeLeft )
                    {
                        pNode = pNode->mpNodeParent.get_node();
                        pRootNode = RBTreeRotateRight(pNode, pRootNode);
                        pNodeAnchor->mpNodeParent.set_node( pRootNode );
                    }

                    pNode->mpNodeParent.get_node()->set_color( kRBTreeColorBlack );
                    pNodeParentParent->set_color(kRBTreeColorRed);
                    pRootNode = RBTreeRotateLeft(pNodeParentParent, pRootNode);
                    pNodeAnchor->mpNodeParent.set_node( pRootNode );
                }
            }
        }

        pNodeAnchor->mpNodeParent.get_node()->set_color(kRBTreeColorBlack);

    } // RBTreeInsert




    /// RBTreeErase
    /// Erase a node from the tree.
    ///
    EASTL_API void RBTreeErase( rbtree_node_base* pNode, rbtree_node_base* pNodeAnchor )
    {
        rbtree_node_base* pNodeRoot = pNodeAnchor->get_parent();
        rbtree_node_base*& pNodeLeftmost = pNodeAnchor->mpNodeLeft;
        rbtree_node_base*& pNodeRightmost = pNodeAnchor->mpNodeRight;

        rbtree_node_base* pNodeSuccessor = pNode;
        rbtree_node_base* pNodeChild = nullptr;
        rbtree_node_base* pNodeChildParent = nullptr;

        // --- Find successor / child ---
        if ( !pNodeSuccessor->mpNodeLeft )
            pNodeChild = pNodeSuccessor->mpNodeRight;
        else if ( !pNodeSuccessor->mpNodeRight )
            pNodeChild = pNodeSuccessor->mpNodeLeft;
        else
        {
            pNodeSuccessor = pNodeSuccessor->mpNodeRight;
            while ( pNodeSuccessor->mpNodeLeft )
                pNodeSuccessor = pNodeSuccessor->mpNodeLeft;

            pNodeChild = pNodeSuccessor->mpNodeRight;
        }

        // --- Remove node ---
        if ( pNodeSuccessor == pNode )
        {
            pNodeChildParent = pNodeSuccessor->get_parent();

            if ( pNodeChild )
                pNodeChild->set_parent( pNodeChildParent );

            if ( pNode == pNodeRoot )
            {
                pNodeRoot = pNodeChild;
                pNodeAnchor->set_parent( pNodeRoot );
            }
            else
            {
                auto* parent = pNode->get_parent();
                if ( pNode == parent->mpNodeLeft )
                    parent->mpNodeLeft = pNodeChild;
                else
                    parent->mpNodeRight = pNodeChild;
            }

            // Update leftmost
            if ( pNode == pNodeLeftmost )
            {
                if ( pNode->mpNodeRight && pNodeChild )
                    pNodeLeftmost = RBTreeGetMinChild( pNodeChild );
                else
                    pNodeLeftmost = pNode->get_parent();
            }

            // Update rightmost
            if ( pNode == pNodeRightmost )
            {
                if ( pNode->mpNodeLeft && pNodeChild )
                    pNodeRightmost = RBTreeGetMaxChild( pNodeChild );
                else
                    pNodeRightmost = pNode->get_parent();
            }
        }
        else
        {
            // --- Successor replacement ---
            pNode->mpNodeLeft->set_parent( pNodeSuccessor );
            pNodeSuccessor->mpNodeLeft = pNode->mpNodeLeft;

            if ( pNodeSuccessor == pNode->mpNodeRight )
            {
                pNodeChildParent = pNodeSuccessor;
            }
            else
            {
                pNodeChildParent = pNodeSuccessor->get_parent();

                if ( pNodeChild )
                    pNodeChild->set_parent( pNodeChildParent );

                pNodeChildParent->mpNodeLeft = pNodeChild;

                pNodeSuccessor->mpNodeRight = pNode->mpNodeRight;
                pNode->mpNodeRight->set_parent( pNodeSuccessor );
            }

            if ( pNode == pNodeRoot )
            {
                pNodeRoot = pNodeSuccessor;
                pNodeAnchor->set_parent( pNodeRoot );
            }
            else
            {
                auto* parent = pNode->get_parent();
                if ( pNode == parent->mpNodeLeft )
                    parent->mpNodeLeft = pNodeSuccessor;
                else
                    parent->mpNodeRight = pNodeSuccessor;
            }

            pNodeSuccessor->set_parent( pNode->get_parent() );

            // swap colors
            char tmp = pNodeSuccessor->get_color();
            pNodeSuccessor->set_color( pNode->get_color() );
            pNode->set_color( tmp );
        }

        // --- Rebalance ---
        if ( pNode->get_color() == kRBTreeColorBlack )
        {
            while ( ( pNodeChild != pNodeRoot ) &&
                    ( !pNodeChild || pNodeChild->get_color() == kRBTreeColorBlack ) )
            {
                if ( pNodeChild == pNodeChildParent->mpNodeLeft )
                {
                    auto* sibling = pNodeChildParent->mpNodeRight;

                    if ( sibling->get_color() == kRBTreeColorRed )
                    {
                        sibling->set_color( kRBTreeColorBlack );
                        pNodeChildParent->set_color( kRBTreeColorRed );
                        pNodeRoot = RBTreeRotateLeft( pNodeChildParent, pNodeRoot );
                        pNodeAnchor->set_parent( pNodeRoot );
                        sibling = pNodeChildParent->mpNodeRight;
                    }

                    if ( ( !sibling->mpNodeLeft || sibling->mpNodeLeft->get_color() == kRBTreeColorBlack ) &&
                         ( !sibling->mpNodeRight || sibling->mpNodeRight->get_color() == kRBTreeColorBlack ) )
                    {
                        sibling->set_color( kRBTreeColorRed );
                        pNodeChild = pNodeChildParent;
                        pNodeChildParent = pNodeChildParent->get_parent();
                    }
                    else
                    {
                        if ( !sibling->mpNodeRight || sibling->mpNodeRight->get_color() == kRBTreeColorBlack )
                        {
                            if ( sibling->mpNodeLeft )
                                sibling->mpNodeLeft->set_color( kRBTreeColorBlack );

                            sibling->set_color( kRBTreeColorRed );
                            pNodeRoot = RBTreeRotateRight( sibling, pNodeRoot );
                            pNodeAnchor->set_parent( pNodeRoot );
                            sibling = pNodeChildParent->mpNodeRight;
                        }

                        sibling->set_color( pNodeChildParent->get_color() );
                        pNodeChildParent->set_color( kRBTreeColorBlack );

                        if ( sibling->mpNodeRight )
                            sibling->mpNodeRight->set_color( kRBTreeColorBlack );

                        pNodeRoot = RBTreeRotateLeft( pNodeChildParent, pNodeRoot );
                        pNodeAnchor->set_parent( pNodeRoot );
                        break;
                    }
                }
                else
                {
                    auto* sibling = pNodeChildParent->mpNodeLeft;

                    if ( sibling->get_color() == kRBTreeColorRed )
                    {
                        sibling->set_color( kRBTreeColorBlack );
                        pNodeChildParent->set_color( kRBTreeColorRed );
                        pNodeRoot = RBTreeRotateRight( pNodeChildParent, pNodeRoot );
                        pNodeAnchor->set_parent( pNodeRoot );
                        sibling = pNodeChildParent->mpNodeLeft;
                    }

                    if ( ( !sibling->mpNodeRight || sibling->mpNodeRight->get_color() == kRBTreeColorBlack ) &&
                         ( !sibling->mpNodeLeft || sibling->mpNodeLeft->get_color() == kRBTreeColorBlack ) )
                    {
                        sibling->set_color( kRBTreeColorRed );
                        pNodeChild = pNodeChildParent;
                        pNodeChildParent = pNodeChildParent->get_parent();
                    }
                    else
                    {
                        if ( !sibling->mpNodeLeft || sibling->mpNodeLeft->get_color() == kRBTreeColorBlack )
                        {
                            if ( sibling->mpNodeRight )
                                sibling->mpNodeRight->set_color( kRBTreeColorBlack );

                            sibling->set_color( kRBTreeColorRed );
                            pNodeRoot = RBTreeRotateLeft( sibling, pNodeRoot );
                            pNodeAnchor->set_parent( pNodeRoot );
                            sibling = pNodeChildParent->mpNodeLeft;
                        }

                        sibling->set_color( pNodeChildParent->get_color() );
                        pNodeChildParent->set_color( kRBTreeColorBlack );

                        if ( sibling->mpNodeLeft )
                            sibling->mpNodeLeft->set_color( kRBTreeColorBlack );

                        pNodeRoot = RBTreeRotateRight( pNodeChildParent, pNodeRoot );
                        pNodeAnchor->set_parent( pNodeRoot );
                        break;
                    }
                }
            }

            if ( pNodeChild )
                pNodeChild->set_color( kRBTreeColorBlack );
        }
    }



} // namespace eastl
























